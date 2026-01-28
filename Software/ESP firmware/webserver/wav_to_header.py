import os
import wave

# CONFIGURATION
INPUT_FOLDER = "audio_files"
OUTPUT_FILE = "sounds.h"

def create_header():
    if not os.path.exists(INPUT_FOLDER):
        os.makedirs(INPUT_FOLDER)
        print(f"Created '{INPUT_FOLDER}' directory. Please put your .wav files there.")
        return

    header_content = "#ifndef SOUNDS_H\n#define SOUNDS_H\n\n#include <pgmspace.h>\n\n"
    found_files = False
    
    print("-" * 40)
    print("Processing Audio Files...")
    print("-" * 40)

    for filename in os.listdir(INPUT_FOLDER):
        if filename.endswith(".wav"):
            file_path = os.path.join(INPUT_FOLDER, filename)
            try:
                with wave.open(file_path, 'rb') as wav_file:
                    # --- CRITICAL CHECKS ---
                    channels = wav_file.getnchannels()
                    sampwidth = wav_file.getsampwidth()
                    framerate = wav_file.getframerate()
                    nframes = wav_file.getnframes()
                    
                    print(f"File: {filename}")
                    print(f"  > Properties: {framerate}Hz, {channels} Channel(s), {sampwidth * 8}-bit")

                    if channels > 1:
                        print(f"  > [ERROR] Stereo detected! Audio will sound distorted. Convert to MONO.")
                        continue # Skip this file
                    
                    if sampwidth != 2:
                        print(f"  > [ERROR] {sampwidth * 8}-bit detected! This code ONLY supports 16-bit.")
                        print(f"  > Please convert {filename} to 16-bit PCM WAV.")
                        continue # Skip this file

                    # Read and convert data
                    frames = wav_file.readframes(nframes)
                    hex_array = ", ".join([f"0x{b:02x}" for b in frames])
                    
                    var_name = filename.replace(".wav", "").replace(" ", "_").replace("-", "_").lower()
                    
                    header_content += f"// File: {filename} | {framerate}Hz | 16-bit Mono\n"
                    header_content += f"const unsigned int {var_name}_rate = {framerate};\n"
                    header_content += f"const unsigned int {var_name}_len = {len(frames)};\n"
                    header_content += f"const unsigned char {var_name}_data[] PROGMEM = {{\n  {hex_array}\n}};\n\n"
                    
                    found_files = True
                    print(f"  > [SUCCESS] Converted to C++ array.")

            except Exception as e:
                print(f"  > [ERROR] Could not process {filename}: {e}")
            print("-" * 40)

    header_content += "#endif\n"

    if found_files:
        with open(OUTPUT_FILE, "w") as f:
            f.write(header_content)
        print(f"\nDONE! '{OUTPUT_FILE}' has been updated.")
        print("Move this file to your Arduino project folder and re-upload.")
    else:
        print("\nNO VALID FILES CONVERTED.")
        print("Please check the errors above and fix your WAV files.")

if __name__ == "__main__":
    create_header()