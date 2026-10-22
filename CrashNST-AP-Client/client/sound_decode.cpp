#include "sound_decode.h"
#include "third_party/fsb_vorbis_setups.inc"
#include <lzma.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>

// stb_vorbis's declarations (its code is compiled in stb_vorbis_impl.cpp)
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
// minimp3's declarations (its code is compiled in minimp3_impl.cpp)
#include "third_party/minimp3.h"

namespace ingame
{
    namespace
    {
        std::int16_t clamp16(int v) { return static_cast<std::int16_t>(std::clamp(v, -32768, 32767)); }
        std::uint16_t u16(const unsigned char *p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
        std::uint32_t u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24; }

        // IMA ADPCM as FMOD stores it (vgmstream's "Xbox IMA"): 0x24-byte frames per channel, each a 4-byte header
        // (first sample, step index) and 32 bytes of nibbles, 64 samples; stereo frames interleave 4 bytes per channel
        bool decode_ima(const unsigned char *data, std::size_t bytes, unsigned channels, Pcm &out)
        {
            static const int steps[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
                                          107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
                                          876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
                                          4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
                                          22385, 24623, 27086, 29794, 32767};
            static const int moves[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
            if (channels < 1 || channels > 2)
                return false;
            const std::size_t frame = 0x24 * channels;
            for (std::size_t base = 0; base + frame <= bytes; base += frame)
            {
                const auto start = out.samples.size();
                out.samples.resize(start + 64 * channels);
                for (unsigned c = 0; c < channels; ++c)
                {
                    const auto header = data + base + 4 * c;
                    int sample = static_cast<std::int16_t>(u16(header));
                    int index = std::clamp(static_cast<int>(static_cast<signed char>(header[2])), 0, 88);
                    out.samples[start + c] = clamp16(sample);
                    for (unsigned i = 1; i < 64; ++i)
                    {
                        const auto at = channels == 2 ? base + 8 + 4 * c + 8 * ((i - 1) / 8) + ((i - 1) % 8) / 2 : base + 4 + (i - 1) / 2;
                        const int nibble = (data[at] >> (((i - 1) & 1) ? 4 : 0)) & 0x0f;
                        const int step = steps[index];
                        int delta = step >> 3;
                        if (nibble & 1)
                            delta += step >> 2;
                        if (nibble & 2)
                            delta += step >> 1;
                        if (nibble & 4)
                            delta += step;
                        sample = std::clamp(nibble & 8 ? sample - delta : sample + delta, -32768, 32767);
                        index = std::clamp(index + moves[nibble], 0, 88);
                        out.samples[start + i * channels + c] = static_cast<std::int16_t>(sample);
                    }
                }
            }
            return !out.samples.empty();
        }
        // FMOD's FADPCM (vgmstream's fadpcm_decoder.c): 0x8c-byte frames per channel, interleaved, 256 samples each
        bool decode_fadpcm(const unsigned char *data, std::size_t bytes, unsigned channels, Pcm &out)
        {
            static const int coefs[8][2] = {{0, 0}, {60, 0}, {122, 60}, {115, 52}, {98, 55}, {0, 0}, {0, 0}, {0, 0}};
            if (channels < 1 || channels > 8)
                return false;
            const std::size_t frame = 0x8c;
            for (std::size_t base = 0; base + frame * channels <= bytes; base += frame * channels)
            {
                const auto start = out.samples.size();
                out.samples.resize(start + 256 * channels);
                for (unsigned c = 0; c < channels; ++c)
                {
                    const auto f = data + base + c * frame;
                    const auto coef_bits = u32(f), shift_bits = u32(f + 4);
                    int hist1 = static_cast<std::int16_t>(u16(f + 8)), hist2 = static_cast<std::int16_t>(u16(f + 10));
                    unsigned n = 0;
                    for (unsigned i = 0; i < 8; ++i)
                    {
                        const auto index = ((coef_bits >> i * 4) & 0x0f) % 0x07;
                        const int shift = 22 - static_cast<int>((shift_bits >> i * 4) & 0x0f);
                        for (unsigned j = 0; j < 4; ++j)
                        {
                            const auto nibbles = u32(f + 0x0c + 0x10 * i + 0x04 * j);
                            for (unsigned k = 0; k < 8; ++k, ++n)
                            {
                                int sample = static_cast<std::int32_t>(((nibbles >> k * 4) & 0x0f) << 28) >> shift;
                                sample = (sample - hist2 * coefs[index][1] + hist1 * coefs[index][0]) >> 6;
                                sample = std::clamp(sample, -32768, 32767);
                                out.samples[start + n * channels + c] = static_cast<std::int16_t>(sample);
                                hist2 = hist1;
                                hist1 = sample;
                            }
                        }
                    }
                }
            }
            return !out.samples.empty();
        }

        // one MPEG audio frame's size from its 4-byte header; 0 when it isn't a frame header
        unsigned mpeg_frame_bytes(const unsigned char *h, unsigned &layer)
        {
            if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0)
                return 0;
            const unsigned version = (h[1] >> 3) & 3; // 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
            layer = 4 - ((h[1] >> 1) & 3);            // 1, 2 or 3
            const unsigned bitrate_index = h[2] >> 4, rate_index = (h[2] >> 2) & 3, padding = (h[2] >> 1) & 1;
            if (version == 1 || layer == 4 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3)
                return 0;
            static const unsigned short bitrates[2][3][15] = {
                {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
                 {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
                 {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}},
                {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
                 {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
                 {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}}};
            static const unsigned rates[3] = {44100, 48000, 32000};
            const bool mpeg1 = version == 3;
            const unsigned rate = rates[rate_index] >> (mpeg1 ? 0 : version == 2 ? 1 : 2);
            const unsigned bitrate = bitrates[mpeg1 ? 0 : 1][layer - 1][bitrate_index] * 1000;
            if (layer == 1)
                return (12 * bitrate / rate + padding) * 4;
            return (layer == 3 && !mpeg1 ? 72 : 144) * bitrate / rate + padding;
        }
        // FSB MPEG (vgmstream's mpeg_custom_utils.c, MPEG_FSB): standard frames, Layer III frames padded to 4 bytes
        // (16 with more than 2 channels); the padding may hold garbage, so frames are found by their sizes. Each frame
        // goes to minimp3 on its own.
        bool decode_mpeg(const unsigned char *data, std::size_t bytes, unsigned channels, Pcm &out, std::string &why)
        {
            mp3dec_t decoder;
            mp3dec_init(&decoder);
            std::vector<short> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
            const unsigned align = channels > 2 ? 16 : 4;
            unsigned frames = 0, skipped = 0;
            for (std::size_t p = 0; p + 4 <= bytes;)
            {
                unsigned layer = 0;
                const auto size = mpeg_frame_bytes(data + p, layer);
                if (!size || p + size > bytes)
                {
                    // past the padding rules: look a little further for the next frame
                    if (++skipped > 64)
                        break;
                    ++p;
                    continue;
                }
                skipped = 0;
                mp3dec_frame_info_t info{};
                const int samples = mp3dec_decode_frame(&decoder, data + p, static_cast<int>(size), pcm.data(), &info);
                if (samples > 0 && info.channels > 0)
                {
                    if (static_cast<unsigned>(info.channels) != channels && out.samples.empty())
                        channels = out.channels = static_cast<unsigned>(info.channels);
                    if (static_cast<unsigned>(info.channels) == channels)
                        out.samples.insert(out.samples.end(), pcm.begin(), pcm.begin() + samples * info.channels);
                    ++frames;
                }
                const auto padding = (layer == 3 || channels > 2) && size % align ? align - size % align : 0;
                p += size + padding;
            }
            if (out.samples.empty())
                return why = "its MPEG data didn't decode", false;
            return true;
        }

        // FMOD's Vorbis setup headers, unpacked once
        const std::vector<unsigned char> &vorbis_setups()
        {
            static std::once_flag once;
            static std::vector<unsigned char> setups;
            std::call_once(once, [] {
                std::vector<unsigned char> unpacked(fsb_vorbis_setups::unpacked_size);
                std::uint64_t memory = UINT64_MAX;
                std::size_t in = 0, out = 0;
                if (lzma_stream_buffer_decode(&memory, 0, nullptr, fsb_vorbis_setups::packed, &in, sizeof(fsb_vorbis_setups::packed), unpacked.data(), &out,
                                              unpacked.size()) == LZMA_OK &&
                    out == unpacked.size())
                    setups = std::move(unpacked);
            });
            return setups;
        }
        // Ogg pages (RFC 3533) around Vorbis packets, so a standard decoder can read FMOD's bare packets
        struct OggWriter
        {
            std::vector<unsigned char> file;
            std::vector<unsigned char> body, lacing;
            unsigned sequence = 0;
            static std::uint32_t crc(const unsigned char *p, std::size_t n)
            {
                static std::uint32_t table[256];
                static std::once_flag once;
                std::call_once(once, [] {
                    for (std::uint32_t i = 0; i < 256; ++i)
                    {
                        std::uint32_t r = i << 24;
                        for (int k = 0; k < 8; ++k)
                            r = r & 0x80000000u ? (r << 1) ^ 0x04c11db7u : r << 1;
                        table[i] = r;
                    }
                });
                std::uint32_t r = 0;
                for (std::size_t i = 0; i < n; ++i)
                    r = (r << 8) ^ table[((r >> 24) ^ p[i]) & 0xff];
                return r;
            }
            bool fits(std::size_t size) const { return lacing.size() + size / 255 + 1 <= 255; }
            void packet(const unsigned char *p, std::size_t size)
            {
                body.insert(body.end(), p, p + size);
                for (; size >= 255; size -= 255)
                    lacing.push_back(255);
                lacing.push_back(static_cast<unsigned char>(size));
            }
            void flush(unsigned char flags, std::uint64_t granule)
            {
                const auto start = file.size();
                const unsigned char head[] = {'O', 'g', 'g', 'S', 0, flags};
                file.insert(file.end(), head, head + 6);
                for (int i = 0; i < 8; ++i)
                    file.push_back(static_cast<unsigned char>(granule >> (8 * i)));
                const std::uint32_t serial = 0x4e535421, seq = sequence++;
                for (int i = 0; i < 4; ++i)
                    file.push_back(static_cast<unsigned char>(serial >> (8 * i)));
                for (int i = 0; i < 4; ++i)
                    file.push_back(static_cast<unsigned char>(seq >> (8 * i)));
                file.insert(file.end(), 4, 0);
                file.push_back(static_cast<unsigned char>(lacing.size()));
                file.insert(file.end(), lacing.begin(), lacing.end());
                file.insert(file.end(), body.begin(), body.end());
                const auto sum = crc(file.data() + start, file.size() - start);
                for (int i = 0; i < 4; ++i)
                    file[start + 22 + i] = static_cast<unsigned char>(sum >> (8 * i));
                body.clear(), lacing.clear();
            }
        };
        // FSB5 Vorbis (vgmstream's vorbis_custom_utils_fsb.c): each packet has a 16-bit size before it; the identification
        // and comment headers are rebuilt (blocksizes 256/2048) and the setup header comes from FMOD's table by its CRC32
        bool decode_vorbis(const unsigned char *data, std::size_t bytes, unsigned channels, unsigned rate, std::uint32_t setup_id, Pcm &out,
                           std::string &why)
        {
            const auto &setups = vorbis_setups();
            const unsigned char *setup = nullptr;
            std::size_t setup_size = 0;
            for (const auto &e : fsb_vorbis_setups::entries)
                if (e.id == setup_id && e.offset + e.size <= setups.size())
                    setup = setups.data() + e.offset, setup_size = e.size;
            if (!setup)
                return why = "its Vorbis setup " + std::to_string(setup_id) + " isn't in FMOD's known list", false;
            unsigned char id[30] = {1, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, static_cast<unsigned char>(channels)};
            std::memcpy(id + 12, &rate, 4); // little-endian, like Windows
            id[28] = (11 << 4) | 8;         // blocksizes: short 256, long 2048
            id[29] = 1;
            const unsigned char comment[25] = {3, 'v', 'o', 'r', 'b', 'i', 's', 9, 0, 0, 0, 'C', 'r', 'a', 's', 'h', 'N', 'S', 'T', 'A', 0, 0, 0, 0, 1};
            OggWriter ogg;
            ogg.packet(id, sizeof(id));
            ogg.flush(0x02, 0);
            ogg.packet(comment, sizeof(comment));
            ogg.packet(setup, setup_size);
            ogg.flush(0x00, 0);
            unsigned packets = 0;
            for (std::size_t p = 0; p + 2 <= bytes;)
            {
                const auto size = u16(data + p);
                if (!size || size == 0xffff || p + 2 + size > bytes || size / 255 + 1 > 255)
                    break;
                if (!ogg.fits(size))
                    ogg.flush(0x00, UINT64_MAX);
                ogg.packet(data + p + 2, size);
                ++packets;
                p += 2 + size;
            }
            if (!packets)
                return why = "it has no Vorbis packets", false;
            ogg.flush(0x04, UINT64_MAX);
            int got_channels = 0, got_rate = 0;
            short *decoded = nullptr;
            const int frames = stb_vorbis_decode_memory(ogg.file.data(), static_cast<int>(ogg.file.size()), &got_channels, &got_rate, &decoded);
            if (frames <= 0 || !decoded || got_channels != static_cast<int>(channels))
            {
                std::free(decoded);
                return why = "its Vorbis data didn't decode (" + std::to_string(frames) + ")", false;
            }
            out.samples.assign(decoded, decoded + static_cast<std::size_t>(frames) * channels);
            std::free(decoded);
            return true;
        }
    }

    bool decode_fmod_sample(const std::string &codec, const unsigned char *data, std::size_t bytes, unsigned channels, unsigned rate,
                            unsigned frames, std::uint32_t vorbis_setup, bool signed8, Pcm &out, std::string &why)
    {
        out = {};
        out.channels = channels, out.rate = rate;
        if (!channels || channels > 8 || !rate || !bytes)
            return why = "its format is unexpected", false;
        bool ok = false;
        if (codec == "PCM16")
        {
            out.samples.resize(bytes / 2 / channels * channels);
            std::memcpy(out.samples.data(), data, out.samples.size() * 2);
            ok = !out.samples.empty();
        }
        else if (codec == "PCM8")
        {
            out.samples.resize(bytes / channels * channels);
            for (std::size_t i = 0; i < out.samples.size(); ++i)
                out.samples[i] = static_cast<std::int16_t>((signed8 ? static_cast<int>(static_cast<signed char>(data[i])) : data[i] - 128) * 256);
            ok = !out.samples.empty();
        }
        else if (codec == "IMA ADPCM")
            ok = decode_ima(data, bytes, channels, out) || (why = "its IMA ADPCM data is too short", false);
        else if (codec == "FADPCM")
            ok = decode_fadpcm(data, bytes, channels, out) || (why = "its FADPCM data is too short", false);
        else if (codec == "MPEG")
            ok = decode_mpeg(data, bytes, channels, out, why);
        else if (codec == "Vorbis")
            ok = decode_vorbis(data, bytes, channels, rate, vorbis_setup, out, why);
        else
            why = "its codec is " + codec + ", which the client can't play yet";
        if (ok && frames && out.samples.size() > static_cast<std::size_t>(frames) * channels)
            out.samples.resize(static_cast<std::size_t>(frames) * channels);
        return ok;
    }
    std::vector<unsigned char> wav_file(const Pcm &pcm)
    {
        const auto size = static_cast<std::uint32_t>(pcm.samples.size() * 2), align = pcm.channels * 2;
        std::vector<unsigned char> wav(44 + size);
        const auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(&wav[at], &v, 4); };
        const auto put16 = [&](std::size_t at, std::uint16_t v) { std::memcpy(&wav[at], &v, 2); };
        std::memcpy(&wav[0], "RIFF", 4), put32(4, 36 + size), std::memcpy(&wav[8], "WAVE", 4);
        std::memcpy(&wav[12], "fmt ", 4), put32(16, 16), put16(20, 1), put16(22, static_cast<std::uint16_t>(pcm.channels));
        put32(24, pcm.rate), put32(28, pcm.rate * align), put16(32, static_cast<std::uint16_t>(align)), put16(34, 16);
        std::memcpy(&wav[36], "data", 4), put32(40, size);
        if (size)
            std::memcpy(&wav[44], pcm.samples.data(), size);
        return wav;
    }
}
