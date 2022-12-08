#include "audio.h"
#include "debug.h"

#ifdef __cplusplus
extern "C" {
#endif

#define fourcc_RIFF 'FFIR'
#define fourcc_DATA 'atad'
#define fourcc_FMT ' tmf'
#define fourcc_WAVE 'EVAW'
#define fourcc_XWMA 'AMWX'
#define fourcc_DPDS 'sdpd'

HRESULT find_chunk(HANDLE h_file, DWORD fourcc, DWORD* dw_chunk_size, DWORD* dw_chunk_data_position) {
    HRESULT hr = S_OK;
    if (INVALID_SET_FILE_POINTER == SetFilePointer(h_file, 0, NULL, FILE_BEGIN)) {
        debug_print(k_print_error, "Failed SetFilePointer1\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD dw_chunk_type;
    DWORD dw_chunk_data_size;
    DWORD dw_riff_data_size = 0;
    DWORD dw_file_type;
    DWORD bytes_read = 0;
    DWORD dw_offset = 0;

    while (hr == S_OK)
    {
        DWORD dw_read;
        if (0 == ReadFile(h_file, &dw_chunk_type, sizeof(DWORD), &dw_read, NULL)) {
            debug_print(k_print_error, "Failed ReadFile1\n");
            hr = HRESULT_FROM_WIN32(GetLastError());
        }

        if (0 == ReadFile(h_file, &dw_chunk_data_size, sizeof(DWORD), &dw_read, NULL)) {
            debug_print(k_print_error, "Failed ReadFile2\n");
            hr = HRESULT_FROM_WIN32(GetLastError());
        }

        switch (dw_chunk_type)
        {
            case fourcc_RIFF:
                dw_riff_data_size = dw_chunk_data_size;
                dw_chunk_data_size = 4;
                if (0 == ReadFile(h_file, &dw_file_type, sizeof(DWORD), &dw_read, NULL)) {
                    debug_print(k_print_error, "Failed ReadFile3\n");
                    hr = HRESULT_FROM_WIN32(GetLastError());
                }
                break;

            default:
                if (INVALID_SET_FILE_POINTER == SetFilePointer(h_file, dw_chunk_data_size, NULL, FILE_CURRENT)) {
                    debug_print(k_print_error, "Failed SetFilePointer2\n");
                    return HRESULT_FROM_WIN32(GetLastError());
                }
        }

        dw_offset += sizeof(DWORD) * 2;

        if (dw_chunk_type == fourcc)
        {
            *dw_chunk_size = dw_chunk_data_size;
            *dw_chunk_data_position = dw_offset;
            return S_OK;
        }

        dw_offset += dw_chunk_data_size;

        if (bytes_read >= dw_riff_data_size) {
            debug_print(k_print_error, "Failed BytesReadWrong\n");
            return S_FALSE;
        }
    }
    return S_OK;

}

HRESULT read_chunk_data(HANDLE h_file, void* buffer, DWORD buffer_size, DWORD* buffer_offset) {
    HRESULT hr = S_OK;
    if (INVALID_SET_FILE_POINTER == SetFilePointer(h_file, *buffer_offset, NULL, FILE_BEGIN)) {
        debug_print(k_print_error, "Failed INVALID_SET_FILE_POINTER\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }
    DWORD dw_read;
    if (0 == ReadFile(h_file, buffer, buffer_size, &dw_read, NULL)) {
        debug_print(k_print_error, "Failed ReadFile4\n");
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    return hr;
}

HRESULT audio_enigne_create(IXAudio2* p_x_audio2, IXAudio2MasteringVoice* p_master_voice, char* src_file, IXAudio2SourceVoice* p_source_voice) {
	HRESULT hr = S_OK;

    WAVEFORMATEXTENSIBLE wfx = { 0 };
    XAUDIO2_BUFFER buffer = { 0 };

	if (FAILED(hr = hr = CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
        debug_print(k_print_error, "Failed CoInitializeEX\n");
        return hr;
	}

	if (FAILED(hr = XAudio2Create(&p_x_audio2, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        debug_print(k_print_error, "Failed XAudio2Create\n");
        return hr;
	}

	if (FAILED(hr = p_x_audio2->CreateMasteringVoice(&p_master_voice))) {
        debug_print(k_print_error, "Failed CreateMasteringVoice\n");
        return hr;
	}

    wchar_t* w_string = new wchar_t[4096];
    MultiByteToWideChar(CP_ACP, 0, src_file, -1, w_string, 4096);

    // Open the file
    HANDLE h_file = CreateFile(
        w_string,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    delete[] w_string;

    if (INVALID_HANDLE_VALUE == h_file) {
        debug_print(k_print_error, "Failed INVALID_HANDLE_VALUE\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (INVALID_SET_FILE_POINTER == SetFilePointer(h_file, 0, NULL, FILE_BEGIN)) {
        debug_print(k_print_error, "Failed INVALID_SET_FILE_POINTER\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD* dw_chunk_size = new(DWORD);
    DWORD* dw_chunk_position = new(DWORD);
    //check the file type, should be fourccWAVE or 'XWMA'
    find_chunk(h_file, fourcc_RIFF, dw_chunk_size, dw_chunk_position);
    DWORD file_type;
    read_chunk_data(h_file, &file_type, sizeof(DWORD), dw_chunk_position);
    if (file_type != fourcc_WAVE) {
        debug_print(k_print_error, "Failed FileType\n");
        return S_FALSE;
    }

    find_chunk(h_file, fourcc_FMT, dw_chunk_size, dw_chunk_position);
    read_chunk_data(h_file, &wfx, *dw_chunk_size, dw_chunk_position);

    //fill out the audio data buffer with the contents of the fourccDATA chunk
    find_chunk(h_file, fourcc_DATA, dw_chunk_size, dw_chunk_position);
    BYTE* p_data_buffer = new BYTE[*dw_chunk_size];
    read_chunk_data(h_file, p_data_buffer, *dw_chunk_size, dw_chunk_position);

    buffer.AudioBytes = *dw_chunk_size;  //size of the audio buffer in bytes
    buffer.pAudioData = p_data_buffer;  //buffer containing audio data
    buffer.Flags = XAUDIO2_END_OF_STREAM; // tell the source voice not to expect any data after this buffer

    if (FAILED(p_x_audio2->CreateSourceVoice( & p_source_voice, (WAVEFORMATEX*)&wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL))) {
        debug_print(k_print_error, "Failed CreateSourceVoice\n");
        return hr;
    }

    if (FAILED(hr = p_source_voice->SubmitSourceBuffer(&buffer))) {
        debug_print(k_print_error, "Failed SubmitSourceBuffer\n");
        return hr;
    }

    if (FAILED(hr = p_source_voice->Start(0))) {
        debug_print(k_print_error, "Failed Start\n");
        return hr;
    }

    delete dw_chunk_size;
    delete dw_chunk_position;

    return hr;
}

#ifdef __cplusplus
}
#endif