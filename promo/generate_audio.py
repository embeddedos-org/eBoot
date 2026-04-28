"""Generate per-segment narration using edge-tts (US English neural voice)."""
import asyncio
import json
import edge_tts
from mutagen.mp3 import MP3

VOICE = "en-US-GuyNeural"
RATE = "+0%"

SEGMENTS = [
    {"id": "intro", "text": "Introducing eBoot. The secure bootloader for EoS."},
    {"id": "f1", "text": "Feature one. Secure Boot Chain. Cryptographic verification of every boot stage from ROM to kernel."},
    {"id": "f2", "text": "Feature two. Multi-Architecture Support. Boots ARM Cortex-M, RISC-V, and x86 targets from a unified codebase."},
    {"id": "f3", "text": "Feature three. OTA Firmware Updates. Atomic over-the-air updates with automatic rollback on failure."},
    {"id": "arch", "text": "Under the hood, eBoot is built with C, Assembly, ARM, and RISC-V. The architecture flows from ROM Stub, to Stage 1, to Stage 2, to Kernel Loader, to OTA Agent."},
    {"id": "cta", "text": "eBoot. Open source and production ready. Visit github dot com slash embeddedos-org slash eBoot."}
]


async def generate():
    durations = {}
    audio_files = []

    for seg in SEGMENTS:
        filename = f"seg_{seg['id']}.mp3"
        communicate = edge_tts.Communicate(seg["text"], VOICE, rate=RATE)
        await communicate.save(filename)
        dur = MP3(filename).info.length
        durations[seg["id"]] = round(dur + 0.5, 1)
        audio_files.append(filename)
        print(f"  {seg['id']}: {dur:.1f}s -> padded {durations[seg['id']]}s")

    with open("durations.json", "w") as f:
        json.dump(durations, f, indent=2)

    import subprocess
    with open("concat_list.txt", "w") as f:
        for af in audio_files:
            f.write(f"file '{af}'\n")

    subprocess.run([
        "ffmpeg", "-y", "-f", "concat", "-safe", "0",
        "-i", "concat_list.txt", "-c", "copy", "narration.mp3"
    ], check=True)

    total = sum(durations.values())
    print(f"\nVoice: {VOICE}")
    print(f"Total narration: {total:.1f}s")

asyncio.run(generate())
