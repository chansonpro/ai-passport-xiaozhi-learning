#include <cassert>
#include <cstdio>
#include "learning_media.h"
#include "ogg_demuxer.h"
int main() {
    for (int i = 0; i < 12; i++) {
        OggDemuxer parser;
        int count = 0, duration = 0;
        parser.OnPacket([&](const uint8_t*, int rate, int ms, size_t size) {
            assert(rate == 16000 && ms == 60 && size > 0 && size < 2048);
            count++;
            duration += ms;
        });
        auto data = learning::kVoice[i];
        parser.Process(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        assert(parser.Finish() && !parser.HasError() && count > 3 && duration < 5000);
        std::printf("PASS voice%d: %d packets, %d ms, firmware Ogg parser\n", i, count, duration);
    }
}
