#include "AskClient.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
constexpr char AuthorizationValue[] = "Bearer " BB_DEVICE_TOKEN; // never log

constexpr int HttpRxBufferBytes = 2048;
constexpr int HttpTxBufferBytes = 1024;
constexpr size_t UploadChunkBytes = 4096;
constexpr size_t ReadChunkBytes = 2048;   // small, so PCM reaches the ring buffer promptly
constexpr size_t MinRingBytes = 2 * ReadChunkBytes;
constexpr uint32_t PollMs = 500;          // socket timeout while waiting, bounds abort latency
constexpr uint32_t PcmBytesPerSecond = 16000 * 2;
constexpr uint32_t PlaybackMarginMs = 3000;
constexpr TickType_t PlaybackPoll = pdMS_TO_TICKS(20);

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
}

AskClient::AskClient(Audio& audio, AudioGenerator& generator, AskConfig config) : audio(audio), generator(generator), config(config){
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

bool AskClient::stopRequested() const{
	return turns.aborted(turn);
}

void AskClient::notify(AskPhase phase) const{
	if(config.onPhase != nullptr){
		config.onPhase(phase, config.phaseCtx);
	}
}

void AskClient::logSecurityPosture(){
	if constexpr(BB_DEV_INSECURE != 0){
		warnInsecure();
	}
}

AskResult AskClient::ask(uint32_t turn, const uint8_t* wav, size_t len){
	this->turn = turn;
	AskResult r;
	if(stopRequested()){
		r.aborted = true;
		return r;
	}

	if constexpr(BB_DEV_INSECURE != 0){
		warnInsecure();
	}

	const int64_t tStart = nowUs();

	esp_http_client_config_t cfg = {};
	cfg.url = AskUrl;
	cfg.method = HTTP_METHOD_POST;
	cfg.timeout_ms = (int) config.connectTimeoutMs;
	cfg.buffer_size = HttpRxBufferBytes;
	cfg.buffer_size_tx = HttpTxBufferBytes;
	cfg.disable_auto_redirect = true; // never follow a redirect off the configured host
	cfg.keep_alive_enable = false;
	if constexpr(Policy == UrlPolicy::Https){
		// TODO(#24): trust only the pinned ISRG Root X1 and X2 (#8 point 2) instead of the full
		// esp_crt_bundle. Hostname verification stays on either way (common_name defaults to the URL host).
		cfg.crt_bundle_attach = esp_crt_bundle_attach;
	}

	HttpHandle http(esp_http_client_init(&cfg));
	if(!http){
		CMF_LOG(AskClient, LogLevel::Warning, "ask: http client init failed");
		return r;
	}
	esp_http_client_handle_t client = http.get();
	esp_http_client_set_header(client, "Content-Type", "audio/wav");
	esp_http_client_set_header(client, "Authorization", AuthorizationValue);

	// Connect (DNS, TCP, TLS) and send the request headers.
	if(esp_http_client_open(client, (int) len) != ESP_OK){
		CMF_LOG(AskClient, LogLevel::Warning, "ask: connect failed");
		return r;
	}
	const int64_t tConnected = nowUs();
	r.connectMs = msBetween(tStart, tConnected);
	notify(AskPhase::Connected);
	const int64_t tUploadStart = nowUs(); // the hook's time is not upload time

	for(size_t sent = 0; sent < len;){
		if(stopRequested()){
			r.aborted = true;
			return r;
		}
		const int chunk = (int) std::min(len - sent, UploadChunkBytes);
		const int n = esp_http_client_write(client, reinterpret_cast<const char*>(wav + sent), chunk);
		if(n <= 0){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: upload failed after %u of %u B", (unsigned) sent, (unsigned) len);
			return r;
		}
		sent += n;
		r.uploadBytes = sent;
	}
	const int64_t tUploaded = nowUs();
	r.uploadMs = msBetween(tUploadStart, tUploaded);

	// Wait for the status line and headers in short polls so abort() is noticed.
	esp_http_client_set_timeout_ms(client, (int) PollMs);
	int64_t contentLength;
	for(;;){
		contentLength = esp_http_client_fetch_headers(client);
		if(contentLength != -ESP_ERR_HTTP_EAGAIN) break;
		if(stopRequested()){
			r.aborted = true;
			return r;
		}
		if(msBetween(tUploaded, nowUs()) >= config.firstByteTimeoutMs){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: no reply within %u ms", (unsigned) config.firstByteTimeoutMs);
			return r;
		}
	}
	if(contentLength < 0){
		CMF_LOG(AskClient, LogLevel::Warning, "ask: reading the reply failed");
		return r;
	}
	r.firstByteMs = msBetween(tUploaded, nowUs());
	r.status = esp_http_client_get_status_code(client);
	r.outcome = outcomeForStatus(r.status);

	if(r.outcome != AskOutcome::Play){
		// No body is read for anything but 200, and none is ever logged (#7 point 7).
		CMF_LOG(AskClient, LogLevel::Info, "ask: HTTP %d, connect %u ms, up %u B in %u ms, first byte %u ms",
				r.status, (unsigned) r.connectMs, (unsigned) r.uploadBytes, (unsigned) r.uploadMs, (unsigned) r.firstByteMs);
		return r;
	}

	std::unique_ptr<uint8_t, CapsFree> buf((uint8_t*) heap_caps_malloc(ReadChunkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	if(!buf){
		CMF_LOG(AskClient, LogLevel::Warning, "ask: out of PSRAM for the read buffer");
		r.outcome = AskOutcome::ThoughtsLost;
		return r;
	}

	WavStreamParser parser;
	SampleAligner aligner;
	std::shared_ptr<PcmStream> stream;
	bool playing = false;
	bool complete = false;
	bool badReply = false;
	size_t pcmBudget = config.maxReplyPcmBytes;
	size_t pcmAccepted = 0; // PCM bytes taken from the reply (within budget)
	size_t pcmQueued = 0;   // PCM bytes written to the ring
	int64_t tFirstByte = 0;
	int64_t tLastByte = 0;
	int64_t tLastProgress = nowUs();

	auto startPlayback = [&](){
		audio.play(&generator, std::make_unique<StreamingPcmSource>(stream));
		playing = true;
		r.firstAudioMs = msBetween(tUploaded, nowUs());
		notify(AskPhase::Playing);
	};

	// Writes whole samples to the ring, waiting for room. False when aborted, when
	// playback was taken away, or when playback stopped draining the ring.
	auto queue = [&](const uint8_t* p, size_t n){
		if(n == 0) return true;
		const int64_t tQueue = nowUs();
		while(!stream->write(p, n, pdMS_TO_TICKS(PollMs))){
			if(stopRequested() || stream->consumerGone()) return false;
			if(!playing){
				startPlayback(); // ring full before the prebuffer mark: start draining it
			}else if(msBetween(tQueue, nowUs()) >= config.stallTimeoutMs){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: playback stopped draining the reply");
				return false;
			}
		}
		pcmQueued += n;
		return true;
	};

	for(;;){
		if(stopRequested() || (playing && stream->consumerGone())){
			r.aborted = true;
			break;
		}

		const int n = esp_http_client_read(client, reinterpret_cast<char*>(buf.get()), (int) ReadChunkBytes);
		if(n == -ESP_ERR_HTTP_EAGAIN){
			if(msBetween(tLastProgress, nowUs()) >= config.stallTimeoutMs){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: reply stalled for %u ms", (unsigned) config.stallTimeoutMs);
				r.truncated = true;
				break;
			}
			continue;
		}
		if(n < 0){
			CMF_LOG(AskClient, LogLevel::Warning, "ask: reading the reply failed after %u B", (unsigned) r.downloadBytes);
			r.truncated = true;
			break;
		}
		if(n == 0){
			complete = esp_http_client_is_complete_data_received(client);
			if(!complete){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: reply ended early after %u B", (unsigned) r.downloadBytes);
				r.truncated = true;
			}
			break;
		}

		tLastByte = tLastProgress = nowUs();
		if(tFirstByte == 0) tFirstByte = tLastByte;
		r.downloadBytes += n;

		const uint8_t* p = buf.get();
		size_t left = n;

		if(parser.state() != WavStreamParser::State::Pcm){
			const auto res = parser.feed(p, left);
			if(res.state == WavStreamParser::State::Invalid){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: reply is not a 16 kHz mono s16 WAV");
				badReply = true;
				break;
			}
			if(res.state == WavStreamParser::State::NeedMore) continue;

			p += res.headerBytes;
			left -= res.headerBytes;

			// Size the ring for the whole reply when its length is known, so the download
			// runs at network speed instead of playback speed.
			std::optional<size_t> known;
			if(const auto d = parser.dataBytes()){
				pcmBudget = std::min<size_t>(pcmBudget, *d);
				known = pcmBudget;
			}else if(contentLength > 0){
				known = std::min<size_t>(pcmBudget, (size_t) contentLength);
			}
			const size_t ringBytes = known ? std::clamp<size_t>((*known + 3) & ~(size_t) 3, MinRingBytes, config.maxRingBytes)
										   : config.minRingBytes;
			stream = PcmStream::create(ringBytes);
			if(!stream){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: out of PSRAM for a %u B ring buffer", (unsigned) ringBytes);
				badReply = true;
				break;
			}
		}

		left = std::min(left, pcmBudget - pcmAccepted);
		pcmAccepted += left;
		const auto aligned = aligner.push(p, left);
		if(!queue(aligned.pair, aligned.pairLen) || !queue(aligned.body, aligned.bodyLen)){
			r.aborted = stopRequested() || stream->consumerGone();
			r.truncated = !r.aborted;
			break;
		}
		// Waiting for room in the ring is back-pressure, not a stalled server.
		tLastProgress = nowUs();

		if(!playing && stream->buffered() >= config.prebufferBytes){
			startPlayback();
		}

		if(pcmAccepted == pcmBudget){
			// Anything after the data chunk (e.g. a trailing LIST chunk) is not audio.
			complete = parser.dataBytes().has_value();
			if(!complete){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: reply longer than %u B of PCM, cut", (unsigned) pcmBudget);
				r.truncated = true;
			}
			break;
		}
	}

	if(tFirstByte != 0){
		r.downloadMs = msBetween(tFirstByte, tLastByte);
		notify(AskPhase::Downloaded);
	}
	// Release the socket and the client's buffers before waiting out playback.
	http.reset();

	if(stream){
		if(r.aborted){
			stream->cancel();
		}else if(complete || playing){
			// Play what arrived; a truncated reply still plays its first part.
			stream->finish();
			if(!playing && stream->buffered() > 0){
				startPlayback();
			}
		}
	}

	if(!r.aborted){
		r.outcome = (complete && pcmQueued > 0 && !badReply) ? AskOutcome::Play : AskOutcome::ThoughtsLost;
	}

	if(playing){
		const uint32_t budgetMs = (uint32_t) (stream->buffered() * 1000 / PcmBytesPerSecond) + PlaybackMarginMs;
		const int64_t tWait = nowUs();
		while(!stream->consumerGone()){
			if(stopRequested() && !r.aborted){
				r.aborted = true;
				stream->cancel(); // the source ends at its next read and Audio stops
			}
			if(msBetween(tWait, nowUs()) >= budgetMs){
				CMF_LOG(AskClient, LogLevel::Warning, "ask: playback did not finish in %u ms", (unsigned) budgetMs);
				stream->cancel();
				r.truncated = true;
				break;
			}
			vTaskDelay(PlaybackPoll);
		}

		const auto stats = stream->stats();
		r.underruns = stats.underruns;
		r.starvedMs = stats.starvedMs;
		r.pcmBytesPlayed = stats.bytesPlayed;
		if(stream->consumerGone() && stats.bytesPlayed < pcmQueued && !r.aborted && !r.truncated){
			// Another sound replaced the reply before it finished.
			r.aborted = true;
		}
	}
	if(r.truncated && !r.aborted){
		r.outcome = AskOutcome::ThoughtsLost;
	}

	CMF_LOG(AskClient, LogLevel::Info,
			"ask: HTTP %d, connect %u ms, up %u B in %u ms, first byte %u ms, down %u B in %u ms, "
			"first audio %u ms, underruns %u (%u ms)%s%s",
			r.status, (unsigned) r.connectMs, (unsigned) r.uploadBytes, (unsigned) r.uploadMs, (unsigned) r.firstByteMs,
			(unsigned) r.downloadBytes, (unsigned) r.downloadMs, (unsigned) r.firstAudioMs,
			(unsigned) r.underruns, (unsigned) r.starvedMs,
			r.truncated ? ", truncated" : "", r.aborted ? ", aborted" : "");
	return r;
}
