#include "combaseapi.h"
#include "xaudio2.h"

#ifdef __cplusplus
extern "C" {
#endif

//Find the chunk of data given the size and position
HRESULT find_chunk(HANDLE h_file, DWORD fourcc, DWORD* dw_chunk_size, DWORD* dw_chunk_data_position);

//Read chunk of data from audio file
HRESULT read_chunk_data(HANDLE h_file, void* buffer, DWORD buffer_size, DWORD* buffer_offset);

// Create an audio engine instance.
HRESULT audio_enigne_create(IXAudio2* p_x_audio2, IXAudio2MasteringVoice* p_master_voice, char* src_file1, IXAudio2SourceVoice* p_source_voice);

#ifdef __cplusplus
}
#endif