#!/usr/bin/env python3
"""Make audition WAVs with GPT-Realtime-2.1; key stays in process memory.

Requires websocket-client and numpy. Run with OPENAI_API_KEY or a hidden prompt.
Existing completed takes are skipped. No game runtime changes are made.
"""
import argparse
import base64
import getpass
import json
import os
from pathlib import Path
import re
import time
import wave

import numpy as np
import websocket

MODEL = "gpt-realtime-2.1"
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "assets/audio/voice_samples/2026-09-04"
TAKES = [
    dict(id="01_npc_watch_out", kind="NPC", title="Watch out", character="Startled pedestrian", voice="cedar", direction="Adult male, rough everyday American voice. A car nearly clips you. One sudden, sharp warning, startled then annoyed. Project across a street; do not scream into the mic.", text="Hey watch out!"),
    dict(id="02_npc_that_smell", kind="NPC", title="That smell", character="Disgusted passerby", voice="coral", direction="Adult woman, dry urban American delivery. You just walked into a truly foul smell. Start with disbelief, wrinkle your nose, put disgust into 'hell' and 'smell'. Comic irritation, fully natural.", text="What the hell is that smell??"),
    dict(id="03_npc_swimming_in_money", kind="NPC", title="Swimming in money", character="Cocky small-time crook", voice="verse", direction="Young adult American man with a light East Coast edge. A private, delighted boast to yourself. You're broke but convinced your big break is coming. Grin on 'swimming in the money'; cocky and a little ridiculous.", text="Wait til they see me swimming in the money."),
    dict(id="04_cutscene_last_chance", kind="Cutscene", title="Last chance", character="Eddie — hungry getaway driver", voice="cedar", direction="American man, late twenties, textured mid-low voice with a light East Coast accent. Alone with your closest friend in a parked car. Begin quiet and honest, pause after 'Same rent', then let stubborn hope turn into resolve. Underplay it; no trailer narration.", text="I been parking rich men's cars since I was sixteen. Same corner. Same rent. Tonight, I pull up to that hotel, and somebody else takes my keys. Just keep the engine running. Let me have this one."),
    dict(id="05_cutscene_clean_job", kind="Cutscene", title="A clean job", character="Mara — precise fixer", voice="marin", direction="American woman, forties, low and composed, clear consonants, small dry smile. Giving a job to someone you like but don't fully trust. Businesslike opening, a deliberate pause before the last sentence, then unmistakable quiet warning.", text="There's an envelope behind the counter at Halloway Gas. Bring it to me sealed. Don't count it, don't smell it, and for God's sake, don't ask the cashier what's inside. Last fellow asked questions. Now I'm hiring you."),
    dict(id="06_cutscene_probable_cause", kind="Cutscene", title="Probable cause", character="Detective Rusk — exhausted investigator", voice="ash", direction="Middle-aged American man, gravelly and sleep-deprived, understated urban accent. You're sitting across a table from a suspect you almost respect. Wry opening, patient interrogation rather than shouting, weariness peeking through at the end.", text="Funny thing about your alibi. Everybody remembers you, but nobody remembers when. That's either a very friendly neighborhood or a very expensive one. Sit down. I've got cold coffee and nothing to lose until morning."),
    dict(id="07_cutscene_wrong_car", kind="Cutscene", title="The wrong car", character="Nina — furious wheelwoman", voice="coral", direction="American woman, early thirties, energetic, brisk and slightly breathless after a botched escape. Anger is covering panic. Bite off the short words. Take one calming breath before 'All right', then snap into practical focus. Dark comedy played straight.", text="I said the blue sedan. That is a hearse. You stole a hearse, and you left the flowers in it. All right. Fine. Get in. If anybody asks, we're late for a funeral, and you're doing a wonderful job looking dead."),
    dict(id="08_cutscene_quiet_threat", kind="Cutscene", title="The quiet threat", character="Mr. Vale — courteous crime boss", voice="ballad", direction="Older adult man, resonant low voice, restrained cultivated American accent. Impeccably polite, dangerous without raising your voice. First sentence genuinely hospitable. A small pause after 'Good'. Let the last two sentences turn cold. Not a caricature or an imitation of any actor.", text="My wife picked out those curtains. Awful things, aren't they? You can say it. Good. Now we know you're capable of telling the truth. Let's try again. Where is my money, and why did your friend leave town without you?"),
    dict(id="09_cutscene_bad_numbers", kind="Cutscene", title="Bad numbers", character="Leon — panicked bookkeeper", voice="echo", direction="Adult American man, thin slightly nasal voice, quick precise speech. Trying to stay professional while frightened. Rush the opening explanation, catch your breath before 'Everybody', lower your voice for the final realization. No added stuttering words.", text="I checked the books three times. The money didn't disappear. Somebody moved it into our account. Our account! Do you understand? Everybody who got robbed is about to think we're the clever ones. I have never wanted to look stupid so badly in my life."),
    dict(id="10_cutscene_last_drink", kind="Cutscene", title="Last drink", character="Ruth — longtime neighborhood bartender", voice="sage", direction="Older American woman, warm worn-in lower register, gentle rasp and unhurried pace. Speaking privately while wiping down an empty bar. Affection under a hard-earned warning. Pause after 'Eyes on the door'. Finish simply, with concern, not melodrama.", text="Your old man sat in that same chair the night he got his big idea. Same smile, too. Eyes on the door. Look, I won't tell you how to live. Just leave enough of yourself to come back through here when the money's gone."),
]


def normalized(text):
    return re.findall(r"[a-z0-9]+", text.lower().replace("'", "").replace("’", ""))


def write_wav(path, pcm):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(24000)
        out.writeframes(pcm)


def generate(take, key, output):
    instructions = (
        "You are an original fictional character voice actor recording an audition for Probable Cause, "
        "a darkly comic open-world crime game. Deliver only the exact supplied dialogue, once, "
        "word for word. No introduction, labels, directions spoken aloud, alternate takes, "
        "extra words, music, sound effects, or background ambience. Natural close-mic clean studio "
        "dialogue. Act to a specific scene partner, not to an audience. Use emotional variation, "
        "natural breaths and motivated pauses; avoid a helpful assistant cadence. Do not imitate "
        "a real person. Character: " + take["character"] + ". Acting direction: " + take["direction"]
    )
    ws = websocket.create_connection(
        "wss://api.openai.com/v1/realtime?model=" + MODEL,
        header=["Authorization: Bearer " + key], timeout=60,
    )
    chunks, transcript, actual_model = [], "", None
    deadline = time.monotonic() + 150
    try:
        ws.send(json.dumps({"type": "session.update", "session": {
            "type": "realtime", "model": MODEL, "output_modalities": ["audio"],
            "instructions": instructions,
            "audio": {"input": {"turn_detection": None}, "output": {
                "format": {"type": "audio/pcm", "rate": 24000}, "voice": take["voice"]}},
        }}))
        while time.monotonic() < deadline:
            event = json.loads(ws.recv())
            kind = event["type"]
            if kind == "error":
                raise RuntimeError(json.dumps(event["error"]))
            if kind == "session.created":
                actual_model = event["session"].get("model")
            if kind == "session.updated":
                actual_model = event["session"].get("model", actual_model)
                print("Recording " + take["id"] + " using " + str(actual_model), flush=True)
                ws.send(json.dumps({"type": "conversation.item.create", "item": {
                    "type": "message", "role": "user", "content": [{"type": "input_text",
                    "text": "Perform this dialogue exactly, now:\n" + take["text"]}]}}))
                ws.send(json.dumps({"type": "response.create"}))
            elif kind == "response.output_audio.delta":
                chunks.append(base64.b64decode(event["delta"]))
            elif kind == "response.output_audio_transcript.done":
                transcript = event["transcript"]
            elif kind == "response.done":
                response = event["response"]
                if response["status"] != "completed":
                    raise RuntimeError(json.dumps(response.get("status_details")))
                if not chunks:
                    raise RuntimeError("Completed response had no audio")
                pcm = b"".join(chunks)
                samples = np.frombuffer(pcm, dtype="<i2").astype(np.float64)
                result = dict(take, requested_model=MODEL, actual_model=actual_model,
                    file=take["id"] + ".wav", transcript=transcript,
                    transcript_matches=normalized(transcript) == normalized(take["text"]),
                    duration_seconds=round(len(samples) / 24000, 3),
                    peak_dbfs=round(float(20 * np.log10(max(1, np.max(np.abs(samples))) / 32768)), 2),
                    clipped_samples=int(np.sum(np.abs(samples) >= 32767)),
                    usage=response.get("usage"), response_id=response.get("id"))
                write_wav(output / result["file"], pcm)
                (output / (take["id"] + ".json")).write_text(json.dumps(result, indent=2) + "\n")
                print(json.dumps({k: result[k] for k in ["id", "duration_seconds", "transcript_matches", "peak_dbfs", "clipped_samples"]}), flush=True)
                return result
        raise TimeoutError("Recording exceeded 150 seconds")
    finally:
        ws.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--scripts-only", action="store_true")
    parser.add_argument("--scripts", type=Path, help="Optional audition JSON for a new casting round")
    args = parser.parse_args()
    takes = json.loads(args.scripts.read_text()) if args.scripts else TAKES
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "scripts.json").write_text(json.dumps(takes, indent=2) + "\n")
    sections = ["# Probable Cause — voice auditions", f"{len(takes)} original synthetic voice auditions. Model: `gpt-realtime-2.1`. Character names are audition concepts. These samples are not wired into gameplay."]
    for take in takes:
        sections.extend(["## " + take["title"], "**" + take["character"] + "** · " + take["kind"] + " · Voice: `" + take["voice"] + "`", take["text"], "Direction: " + take["direction"]])
    (args.output / "SCRIPTS.md").write_text("\n\n".join(sections) + "\n")
    if args.scripts_only:
        return
    key = os.environ.get("OPENAI_API_KEY") or getpass.getpass("OpenAI API key (hidden): ")
    # Markdown may escape an underscore when a key is pasted into chat.
    key = key.strip().replace("\\_", "_")
    for take in takes:
        meta = args.output / (take["id"] + ".json")
        if meta.exists() and (args.output / (take["id"] + ".wav")).exists():
            print("Already recorded " + take["id"], flush=True)
            continue
        try:
            generate(take, key, args.output)
        except Exception as exc:
            # Never echo authorization material, including in network exceptions.
            print("Recording failed: " + str(exc).replace(key, "[REDACTED]"), flush=True)
            raise SystemExit(1)


if __name__ == "__main__":
    main()
