#include "AskClient.h"
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <lwip/netdb.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <Log/Log.h>
#include <Services/Audio/Audio.h>
#include "Audio/StreamingPcmSource.h"
#include "Util/Elapsed.h"
#include "SampleAligner.h"
#include "UrlPolicy.h"
#include "WavStreamParser.h"
// The only translation unit that sees the API URL and device token.
#include "../../secrets.h"

#if !defined(BB_API_URL) || !defined(BB_DEVICE_TOKEN) || !defined(BB_DEV_INSECURE)
#error "secrets.h must define BB_API_URL, BB_DEVICE_TOKEN and BB_DEV_INSECURE, see main/secrets.h.example"
#endif

DEFINE_LOG(AskClient)

namespace {
constexpr UrlPolicy Policy = checkApiUrl(BB_API_URL, BB_DEV_INSECURE != 0);
static_assert(Policy != UrlPolicy::Rejected,
			  "BB_API_URL must be an https:// base URL (http:// only with BB_DEV_INSECURE 1), "
			  "without a trailing slash, query, fragment or credentials");

constexpr char AskUrl[] = BB_API_URL "/v1/ask";

constexpr std::string_view ApiHost = apiHost(BB_API_URL);
static_assert(!ApiHost.empty(), "BB_API_URL has no host");
// NUL-terminated for getaddrinfo.
constexpr auto ApiHostZ = [](){
	std::array<char, ApiHost.size() + 1> host{};
	ApiHost.copy(host.data(), ApiHost.size());
	return host;
}();
constexpr char AuthorizationValue[] = "Bearer " BB_DEVICE_TOKEN; // never log

constexpr int HttpRxBufferBytes = 2048;
constexpr int HttpTxBufferBytes = 1024;
constexpr size_t UploadChunkBytes = 4096;
constexpr size_t ReadChunkBytes = 2048;   // small, so PCM reaches the ring buffer promptly
constexpr uint32_t PollMs = 500;          // socket timeout while waiting, bounds abort latency

constexpr uint32_t StallTimeoutMs = 12000;                 // longest gap between reply bytes once the reply has started
constexpr size_t PrebufferBytes = 16 * 1024;               // PCM buffered before playback starts (0.5 s)
constexpr size_t MinRingBytes = 2 * ReadChunkBytes;
constexpr size_t UnknownLengthRingBytes = 64 * 1024;       // ring when the reply length is unknown (2 s)
constexpr size_t MaxRingBytes = 640 * 1024;                // a 20 s reply (#7 point 5) fits without back-pressure
constexpr size_t MaxReplyPcmBytes = 30 * PcmStream::BytesPerSecond; // played at most (30 s); the server caps replies at ~20 s

struct HttpCleanup {
	void operator()(esp_http_client* client) const{ esp_http_client_cleanup(client); }
};

using HttpHandle = std::unique_ptr<esp_http_client, HttpCleanup>;

struct CapsFree {
	void operator()(uint8_t* p) const{ heap_caps_free(p); }
};

void warnInsecure(){
	CMF_LOG(AskClient, LogLevel::Warning, "!!! INSECURE DEV BUILD: BB_DEV_INSECURE is 1 !!!");
	if constexpr(Policy == UrlPolicy::DevInsecureHttp){
		CMF_LOG(AskClient, LogLevel::Warning, "!!! The API URL is plain http://: audio and the device token cross the network unencrypted. Never ship this. !!!");
	}
}

/**
 * One /v1/ask exchange, step by step: connect, upload, wait for the status, stream the reply
 * into playback, wait for playback. Each step records its numbers in `result`.
 */
class AskExchange {
public:
	AskExchange(Audio& audio, AudioGenerator& generator, const AskPhaseHook& onPhase, const TurnAbort& turns, uint32_t turn)
			: audio(audio), generator(generator), onPhase(onPhase), turns(turns), turn(turn){}

	AskResult run(const uint8_t* wav, size_t len){
		if(stopRequested()){
			result.aborted = true;
			return result;
		}
		if constexpr(BB_DEV_INSECURE != 0){
			warnInsecure();
		}

		tStart = nowUs();
		if(!resolve() || !connect(len) || !upload(wav, len) || !awaitStatus()){
			return result;
		}
		if(result.outcome != AskOutcome::Play){
			// No body is read for anything but 200, and none is ever logged (#7 point 7).
			CMF_LOG(AskClient, LogLevel::Info, "ask: HTTP %d, dns %u ms, connect %u ms, up %u B in %u ms, first byte %u ms",
					result.status, (unsigned) result.dnsMs, (unsigned) result.connectMs, (unsigned) result.uploadBytes, (unsigned) result.uploadMs,
					(unsigned) result.firstByteMs);
			return result;
		}

		downloadReply();
		// Release the socket and the client's buffers before waiting out playback.
		http.reset();
		finishReply();
		logSummary();
		return result;
	}

private:
	Audio& audio;
	AudioGenerator& generator;
	const AskPhaseHook& onPhase;
	const TurnAbort& turns;
	const uint32_t turn;

	AskResult result;
	HttpHandle http;
	int64_t tStart = 0;
	int64_t tUploaded = 0;
	int64_t contentLength = -1;

	// The reply.
	WavStreamParser parser;
	SampleAligner aligner;
	std::shared_ptr<PcmStream> stream;
	bool playing = false;
	bool complete = false;  // the whole reply arrived
	bool badReply = false;  // not a usable WAV, or no memory for it
	size_t pcmBudget = MaxReplyPcmBytes;
	size_t pcmAccepted = 0; // PCM bytes taken from the reply (within budget)
	size_t pcmQueued = 0;   // PCM bytes written to the ring
	int64_t tFirstByte = 0;
	int64_t tLastByte = 0;

	bool stopRequested() const{
		return turns.aborted(turn);
	}

	void notify(AskPhase phase) const{
		if(onPhase){
			onPhase(phase);
		}
	}

	esp_http_client_handle_t client() const{
		return http.get();
	}

	/**
	 * Resolves the API host on its own, so DNS time is measured apart from connect time and counts
	 * against the connect deadline. esp_http_client's own lookup then hits lwIP's DNS cache.
	 */
	bool resolve(){
		addrinfo hints = {};
		hints.ai_family = AF_UNSPEC; // as esp-tls asks
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* found = nullptr;
		const int err = getaddrinfo(ApiHostZ.data(), nullptr, &hints, &found);
		result.dnsMs = msSince(tStart);
		if(found != nullptr){
			freeaddrinfo(found);
		}
		if(err != 0 || found == nullptr){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: DNS lookup failed (%d) after %u ms", err, (unsigned) result.dnsMs);
			return false;
		}
		if(result.dnsMs >= AskClient::ConnectTimeoutMs){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: DNS took %u ms, past the %u ms connect deadline",
					(unsigned) result.dnsMs, (unsigned) AskClient::ConnectTimeoutMs);
			return false;
		}
		return true;
	}

	/** TCP, TLS and the request headers, within what is left of the connect deadline. */
	bool connect(size_t len){
		const int64_t tResolved = nowUs();
		esp_http_client_config_t cfg = {};
		cfg.url = AskUrl;
		cfg.method = HTTP_METHOD_POST;
		cfg.timeout_ms = (int) (AskClient::ConnectTimeoutMs - result.dnsMs);
		cfg.buffer_size = HttpRxBufferBytes;
		cfg.buffer_size_tx = HttpTxBufferBytes;
		cfg.disable_auto_redirect = true; // never follow a redirect off the configured host
		cfg.keep_alive_enable = false;
		if constexpr(Policy == UrlPolicy::Https){
			// TODO(#24): trust only the pinned ISRG Root X1 and X2 (#8 point 2) instead of the full
			// esp_crt_bundle. Hostname verification stays on either way (common_name defaults to the URL host).
			cfg.crt_bundle_attach = esp_crt_bundle_attach;
		}

		http.reset(esp_http_client_init(&cfg));
		if(!http){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: http client init failed");
			return false;
		}
		esp_http_client_set_header(client(), "Content-Type", "audio/wav");
		esp_http_client_set_header(client(), "Authorization", AuthorizationValue);

		if(esp_http_client_open(client(), (int) len) != ESP_OK){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: connect failed after %u ms", (unsigned) msSince(tResolved));
			return false;
		}
		result.connectMs = msSince(tResolved);
		// The handshake can overrun the budget it was given (see AskClient::ConnectTimeoutMs): hold the line here.
		if(const uint32_t total = msSince(tStart); total > AskClient::ConnectTimeoutMs){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: connected after %u ms, past the %u ms deadline",
					(unsigned) total, (unsigned) AskClient::ConnectTimeoutMs);
			return false;
		}
		// From here the timeout bounds each upload write.
		esp_http_client_set_timeout_ms(client(), (int) AskClient::ConnectTimeoutMs);
		notify(AskPhase::Connected);
		return true;
	}

	bool upload(const uint8_t* wav, size_t len){
		const int64_t tUploadStart = nowUs(); // after the Connected hook: its time is not upload time
		for(size_t sent = 0; sent < len;){
			if(stopRequested()){
				result.aborted = true;
				return false;
			}
			const int chunk = (int) std::min(len - sent, UploadChunkBytes);
			const int n = esp_http_client_write(client(), reinterpret_cast<const char*>(wav + sent), chunk);
			if(n <= 0){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: upload failed after %u of %u B", (unsigned) sent, (unsigned) len);
				return false;
			}
			sent += n;
			result.uploadBytes = sent;
		}
		tUploaded = nowUs();
		result.uploadMs = msBetween(tUploadStart, tUploaded);
		return true;
	}

	/** Waits for the status line and headers in short polls, so an abort is noticed. */
	bool awaitStatus(){
		esp_http_client_set_timeout_ms(client(), (int) PollMs);
		for(;;){
			contentLength = esp_http_client_fetch_headers(client());
			if(contentLength != -ESP_ERR_HTTP_EAGAIN) break;
			if(stopRequested()){
				result.aborted = true;
				return false;
			}
			if(msSince(tUploaded) >= AskClient::FirstByteTimeoutMs){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: no reply within %u ms", (unsigned) AskClient::FirstByteTimeoutMs);
				return false;
			}
		}
		if(contentLength < 0){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: reading the reply failed");
			return false;
		}
		result.firstByteMs = msSince(tUploaded);
		result.status = esp_http_client_get_status_code(client());
		result.outcome = outcomeForStatus(result.status);
		return true;
	}

	/** Reads a 200 reply into the ring buffer, starting playback once enough is buffered. */
	void downloadReply(){
		std::unique_ptr<uint8_t, CapsFree> buf((uint8_t*) heap_caps_malloc(ReadChunkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
		if(!buf){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: out of PSRAM for the read buffer");
			badReply = true;
			return;
		}

		int64_t tLastProgress = nowUs();
		for(;;){
			if(stopRequested() || (playing && stream->consumerGone())){
				result.aborted = true;
				break;
			}

			const int n = esp_http_client_read(client(), reinterpret_cast<char*>(buf.get()), (int) ReadChunkBytes);
			if(n == -ESP_ERR_HTTP_EAGAIN){
				if(msSince(tLastProgress) >= StallTimeoutMs){
					CMF_LOG(AskClient, LogLevel::Warning, "ask: reply stalled for %u ms", (unsigned) StallTimeoutMs);
					result.truncated = true;
					break;
				}
				continue;
			}
			if(n < 0){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: reading the reply failed after %u B", (unsigned) result.downloadBytes);
				result.truncated = true;
				break;
			}
			if(n == 0){
				complete = esp_http_client_is_complete_data_received(client());
				if(!complete){
					CMF_LOG(AskClient, LogLevel::Warning, "ask: reply ended early after %u B", (unsigned) result.downloadBytes);
					result.truncated = true;
				}
				break;
			}

			tLastByte = tLastProgress = nowUs();
			if(tFirstByte == 0) tFirstByte = tLastByte;
			result.downloadBytes += n;

			const uint8_t* p = buf.get();
			size_t left = n;

			if(parser.state() != WavStreamParser::State::Pcm){
				const auto parsed = parser.feed(p, left);
				if(parsed.state == WavStreamParser::State::Invalid){
					CMF_LOG(AskClient, LogLevel::Warning, "ask: reply is not a 16 kHz mono s16 WAV");
					badReply = true;
					break;
				}
				if(parsed.state == WavStreamParser::State::NeedMore) continue;

				p += parsed.headerBytes;
				left -= parsed.headerBytes;
				if(!createStream()){
					badReply = true;
					break;
				}
			}

			if(!acceptPcm(p, left)) break;
			// Waiting for room in the ring is back-pressure, not a stalled server.
			tLastProgress = nowUs();

			if(!playing && stream->buffered() >= PrebufferBytes){
				startPlayback();
			}

			if(pcmAccepted == pcmBudget){
				// Anything after the data chunk (e.g. a trailing LIST chunk) is not audio.
				complete = parser.dataBytes().has_value();
				if(!complete){
					CMF_LOG(AskClient, LogLevel::Warning, "ask: reply longer than %u B of PCM, cut", (unsigned) pcmBudget);
					result.truncated = true;
				}
				break;
			}
		}

		if(tFirstByte != 0){
			result.downloadMs = msBetween(tFirstByte, tLastByte);
			notify(AskPhase::Downloaded);
		}
	}

	/**
	 * Creates the ring buffer once the WAV header is through. It holds the whole reply when its length
	 * is known, so the download runs at network speed instead of playback speed.
	 */
	bool createStream(){
		std::optional<size_t> replyPcmBytes;
		if(const auto dataBytes = parser.dataBytes()){
			pcmBudget = std::min<size_t>(pcmBudget, *dataBytes);
			replyPcmBytes = pcmBudget;
		}else if(contentLength > 0){
			replyPcmBytes = std::min<size_t>(pcmBudget, (size_t) contentLength);
		}
		const size_t ringBytes = replyPcmBytes ? std::clamp<size_t>((*replyPcmBytes + 3) & ~(size_t) 3, MinRingBytes, MaxRingBytes)
											   : UnknownLengthRingBytes;
		stream = PcmStream::create(ringBytes);
		if(!stream){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: out of PSRAM for a %u B ring buffer", (unsigned) ringBytes);
			return false;
		}
		return true;
	}

	/** Queues reply PCM within the budget as whole samples. False when the turn has to stop reading. */
	bool acceptPcm(const uint8_t* p, size_t n){
		n = std::min(n, pcmBudget - pcmAccepted);
		pcmAccepted += n;
		const auto aligned = aligner.push(p, n);
		if(queue(aligned.pair, aligned.pairLen) && queue(aligned.body, aligned.bodyLen)){
			return true;
		}
		result.aborted = stopRequested() || stream->consumerGone();
		result.truncated = !result.aborted;
		return false;
	}

	/**
	 * Writes whole samples to the ring, waiting for room. False when aborted, when playback
	 * was taken away, or when playback stopped draining the ring.
	 */
	bool queue(const uint8_t* p, size_t n){
		if(n == 0) return true;
		const int64_t tQueue = nowUs();
		while(!stream->write(p, n, pdMS_TO_TICKS(PollMs))){
			if(stopRequested() || stream->consumerGone()) return false;
			if(!playing){
				startPlayback(); // ring full before the prebuffer mark: start draining it
			}else if(msSince(tQueue) >= StallTimeoutMs){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: playback stopped draining the reply");
				return false;
			}
		}
		pcmQueued += n;
		return true;
	}

	void startPlayback(){
		// Queued, not play(): a filler line that is still speaking finishes first (decisions #5). The ring keeps
		// filling meanwhile. Audio::stop() drops the queued reply too, which ends the turn as aborted.
		audio.enqueue(&generator, std::make_unique<StreamingPcmSource>(stream));
		playing = true;
		result.firstAudioMs = msSince(tUploaded);
		notify(AskPhase::Playing);
	}

	/** Plays what arrived (a truncated reply still plays its first part), then sets the outcome. */
	void finishReply(){
		if(stream){
			if(result.aborted){
				stream->cancel();
			}else if(complete || playing){
				stream->finish();
				if(!playing && stream->buffered() > 0){
					startPlayback();
				}
			}
		}

		if(!result.aborted){
			result.outcome = (complete && pcmQueued > 0 && !badReply) ? AskOutcome::Play : AskOutcome::ThoughtsLost;
		}
		if(playing){
			waitPlayback();
		}
		if(result.truncated && !result.aborted){
			result.outcome = AskOutcome::ThoughtsLost;
		}
	}

	void waitPlayback(){
		switch(stream->waitPlayed([this](){ return stopRequested(); })){
			case PcmStream::PlaybackEnd::Played:
				break;
			case PcmStream::PlaybackEnd::Stopped:
				result.aborted = true;
				break;
			case PcmStream::PlaybackEnd::Replaced:
				// Another sound took over before the reply finished.
				if(!result.truncated) result.aborted = true;
				break;
			case PcmStream::PlaybackEnd::TimedOut:
				CMF_LOG(AskClient, LogLevel::Warning, "ask: playback did not finish in time");
				result.truncated = true;
				break;
		}

		const auto stats = stream->stats();
		result.underruns = stats.underruns;
		result.starvedMs = stats.starvedMs;
		result.pcmBytesPlayed = stats.bytesPlayed;
	}

	void logSummary() const{
		CMF_LOG(AskClient, LogLevel::Info,
				"ask: HTTP %d, dns %u ms, connect %u ms, up %u B in %u ms, first byte %u ms, down %u B in %u ms, "
				"first audio %u ms, underruns %u (%u ms)%s%s",
				result.status, (unsigned) result.dnsMs, (unsigned) result.connectMs, (unsigned) result.uploadBytes, (unsigned) result.uploadMs,
				(unsigned) result.firstByteMs, (unsigned) result.downloadBytes, (unsigned) result.downloadMs,
				(unsigned) result.firstAudioMs, (unsigned) result.underruns, (unsigned) result.starvedMs,
				result.truncated ? ", truncated" : "", result.aborted ? ", aborted" : "");
	}
};
}

AskClient::AskClient(Audio& audio, AudioGenerator& generator, AskPhaseHook onPhase)
		: audio(audio), generator(generator), onPhase(std::move(onPhase)){
}

uint32_t AskClient::beginTurn(){
	return turns.arm();
}

bool AskClient::aborted(uint32_t turn){
	return turns.aborted(turn);
}

void AskClient::abortInFlight(){
	turns.abort();
}

void AskClient::logSecurityPosture(){
	if constexpr(BB_DEV_INSECURE != 0){
		warnInsecure();
	}
}

AskResult AskClient::ask(uint32_t turn, const uint8_t* wav, size_t len){
	return AskExchange(audio, generator, onPhase, turns, turn).run(wav, len);
}
