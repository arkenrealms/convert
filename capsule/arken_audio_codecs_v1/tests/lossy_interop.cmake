# Independent interoperability of the lossy codecs. FFmpeg (its own native
# Vorbis/Opus/MP3 decoders, and libvorbis/libopus/libmp3lame through FFmpeg as
# encoders) is a reference oracle only, never a product dependency.
#  1. FFmpeg decodes Arken's Ogg Vorbis, Ogg Opus and MP3 (CBR and VBR):
#     frame count within one codec frame of the source, alignment lag 0, SNR floor.
#  2. Arken decodes FFmpeg's Ogg Vorbis, Ogg Opus and MP3 (with ID3v2 and the
#     Xing/LAME tag FFmpeg writes): same checks against the source.
#  3. Arken decodes FFmpeg's 5.1 Vorbis and 5.1 Opus (mapping family 1): each
#     WAVE-order channel matches its own source channel (order and mask).
# Usage: cmake -DTOOL=... -DFFMPEG=... -DWORK=... -P lossy_interop.cmake
foreach(required TOOL FFMPEG WORK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "lossy_interop.cmake requires -D${required}")
  endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "command failed (${code}): ${ARGN}\n${out}\n${err}")
  endif()
  if(NOT out STREQUAL "")
    string(STRIP "${out}" out)
    message(STATUS "${out}")
  endif()
endfunction()
set(ff "${FFMPEG}" -nostdin -v error -y)
run("${TOOL}" --emit "${WORK}")
# Floors: waveform SNR of perceptual codecs on the programme material (speech,
# music, transients, silence); see NOTES.md. Tolerance: frames of difference
# allowed (0 = exact).
foreach(ch 1 2)
  set(p44 "${WORK}/prog44_${ch}.raw")
  set(p48 "${WORK}/prog48_${ch}.raw")
  # 1. FFmpeg decodes Arken.
  foreach(spec "arken_${ch}.ogg|${p44}|44100" "arken_${ch}.opus|${p48}|48000"
               "arken_${ch}.mp3|${p44}|44100" "arken_vbr_${ch}.mp3|${p44}|44100")
    string(REPLACE "|" ";" parts "${spec}")
    list(GET parts 0 file)
    list(GET parts 1 reference)
    list(GET parts 2 rate)
    run(${ff} -i "${WORK}/${file}" -f s16le -c:a pcm_s16le -ac ${ch} -ar ${rate} "${WORK}/${file}.ffmpeg.raw")
    run("${TOOL}" --compare-raw "${reference}" "${WORK}/${file}.ffmpeg.raw" ${rate} ${ch} 10 0)
  endforeach()
  # 2. Arken decodes FFmpeg's encoders.
  run(${ff} -f s16le -ar 44100 -ac ${ch} -i "${p44}" -c:a libvorbis -q:a 5 "${WORK}/ffmpeg_${ch}.ogg")
  run(${ff} -f s16le -ar 48000 -ac ${ch} -i "${p48}" -c:a libopus -b:a 128k "${WORK}/ffmpeg_${ch}.opus")
  run(${ff} -f s16le -ar 44100 -ac ${ch} -i "${p44}" -c:a libmp3lame -b:a 192k "${WORK}/ffmpeg_${ch}.mp3")
  run(${ff} -f s16le -ar 44100 -ac ${ch} -i "${p44}" -c:a libmp3lame -q:a 2 "${WORK}/ffmpeg_vbr_${ch}.mp3")
  run("${TOOL}" --decode-compare vorbis "${WORK}/ffmpeg_${ch}.ogg" "${p44}" 44100 ${ch} 10 0)
  run("${TOOL}" --decode-compare opus "${WORK}/ffmpeg_${ch}.opus" "${p48}" 48000 ${ch} 10 0)
  run("${TOOL}" --decode-compare mp3 "${WORK}/ffmpeg_${ch}.mp3" "${p44}" 44100 ${ch} 10 0)
  run("${TOOL}" --decode-compare mp3 "${WORK}/ffmpeg_vbr_${ch}.mp3" "${p44}" 44100 ${ch} 10 0)
endforeach()
# 3. Multichannel: FFmpeg 5.1 (FL FR FC LFE BL BR) -> Vorbis order / Opus family 1 -> Arken WAVE order.
set(s6 "${WORK}/surround48.raw")
run(${ff} -f s16le -ar 48000 -ch_layout 5.1 -i "${s6}" -c:a libvorbis -q:a 6 "${WORK}/ffmpeg_51.ogg")
run(${ff} -f s16le -ar 48000 -ch_layout 5.1 -i "${s6}" -c:a libopus -b:a 384k -mapping_family 1 "${WORK}/ffmpeg_51.opus")
run("${TOOL}" --decode-compare vorbis "${WORK}/ffmpeg_51.ogg" "${s6}" 48000 6 10 0)
run("${TOOL}" --decode-compare opus "${WORK}/ffmpeg_51.opus" "${s6}" 48000 6 10 0)
