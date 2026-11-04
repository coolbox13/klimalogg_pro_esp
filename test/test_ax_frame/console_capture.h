// Demodulated FSK bits (before NRZI) of one console current-weather
// frame, recorded with an RTL-SDR on 2026-11-04. MSB first.
#pragma once
#include <stddef.h>
constexpr size_t kConsoleCaptureBits = 2423;
constexpr const char* kConsoleCaptureHex =
    "3500057faabae76a9c851f8aeef201c085718c399c1c965eb75db7ec3cac4d0b6faad4e1"
    "bdf298318f6657b036bd16e9838a9f13942122ac52de28df34ce7c166e78f419510ad2dd"
    "49f2af02b28352013a642e66274fbd8ad42cdd84ec2fde58ac37fddd4e2a9d4976fe8ebe"
    "85153a0f97e380ec242dc5c39c5941f0168d9a86339293eb1c71fe43aeed7ea6dd98fde2"
    "3ba21253754bd5bd8743e2eb76b89b204b44dabd53d74e238f92a58d5d4bc64d42c0a9f3"
    "52e11ef8f9de78912cc3c30fb71a73b8643ca84d496bab96f589bbc6f8a16fdfd38b4823"
    "40437d8400e4d7a688225fad4c2ffd71b08e493a2ea66467122472c773725705c2475ccf"
    "fbb5df1e874aae9627b0f18506f435ab26012a8ba4660dc68fdeef54d85f948942d08c60"
    "80384398184d88fe4b9b9c74097a92";
