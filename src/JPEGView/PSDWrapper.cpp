/*  This file uses code adapted from SAIL (https://github.com/HappySeaFox/sail/blob/master/src/sail-codecs/psd/psd.c)
    See the original copyright notice below:

    Copyright (c) 2022 Dmitry Baryshev

    The MIT License

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

/* Documentation of the PSD file format can be found here: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
    Tags can also be found here: https://exiftool.org/TagNames/Photoshop.html

    Useful image resources:
    0x0409 1033 (Photoshop 4.0) Thumbnail resource for Photoshop 4.0 only.See See Thumbnail resource format.
    0x040C 1036 (Photoshop 5.0) Thumbnail resource(supersedes resource 1033).See See Thumbnail resource format.
    0x040F 1039 (Photoshop 5.0) ICC Profile.The raw bytes of an ICC(International Color Consortium) format profile.See ICC1v42_2006 - 05.pdf in the Documentation folder and icProfileHeader.h in Sample Code\Common\Includes .
    0x0411 1041 (Photoshop 5.0) ICC Untagged Profile. 1 byte that disables any assumed profile handling when opening the file. 1 = intentionally untagged.
    0x0417 1047 (Photoshop 6.0) Transparency Index. 2 bytes for the index of transparent color, if any.
    0x0419 1049 (Photoshop 6.0) Global Altitude. 4 byte entry for altitude
    0x041D 1053 (Photoshop 6.0) Alpha Identifiers. 4 bytes of length, followed by 4 bytes each for every alpha identifier.
    Get alpha identifier and look at its index number, if not 0 abort
    0x0421 1057 (Photoshop 6.0) Version Info. 4 bytes version, 1 byte hasRealMergedData, Unicode string : writer name, Unicode string : reader name, 4 bytes file version.
    0x0422 1058 (Photoshop 7.0) EXIF data 1. See http://www.kodak.com/global/plugins/acrobat/en/service/digCam/exifStandard2.pdf
    0x0423 1059 (Photoshop 7.0) EXIF data 3. See http://www.kodak.com/global/plugins/acrobat/en/service/digCam/exifStandard2.pdf
    Not sure what 0x0423 is.
*/

#include "stdafx.h"
#include "PSDWrapper.h"
#include "MaxImageDef.h"
#include "Helpers.h"
#include "TJPEGWrapper.h"
#include "ICCProfileTransform.h"
#include "SettingsProvider.h"

// Throw exception if bShouldThrow is true. Setting a breakpoint in here is useful for debugging
void PsdReader::ThrowIf(bool bShouldThrow) {
    if (bShouldThrow) {
        throw 1;
    }
}

// Read exactly sz bytes of the file into p
void PsdReader::ReadFromFile(void* dst, HANDLE file, DWORD sz) {
    DWORD nNumBytesRead;
    ThrowIf(!(::ReadFile(file, dst, sz, &nNumBytesRead, NULL) && nNumBytesRead == sz));
}

// Read and return an unsigned 64-bit int from file
unsigned long long PsdReader::ReadUInt64FromFile(HANDLE file) {
    unsigned long long val;
    ReadFromFile(&val, file, 8);
    return _byteswap_uint64(val);
}

// Read and return an unsigned int from file
unsigned int PsdReader::ReadUIntFromFile(HANDLE file) {
    unsigned int val;
    ReadFromFile(&val, file, 4);
    return _byteswap_ulong(val);
}

// Read and return an unsigned short from file
unsigned short PsdReader::ReadUShortFromFile(HANDLE file) {
    unsigned short val;
    ReadFromFile(&val, file, 2);
    return _byteswap_ushort(val);
}

// Read and return an unsigned char from file
unsigned char PsdReader::ReadUCharFromFile(HANDLE file) {
    unsigned char val;
    ReadFromFile(&val, file, 1);
    return val;
}

// Move file pointer by offset from current position
void PsdReader::SeekFile(HANDLE file, LONG offset) {
    ThrowIf(::SetFilePointer(file, offset, NULL, FILE_CURRENT) == INVALID_SET_FILE_POINTER);
}

// Move file pointer to offset from beginning of file
void PsdReader::SeekFileFromStart(HANDLE file, LONG offset) {
    ThrowIf(::SetFilePointer(file, offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER);
}

// Get current position in the file
unsigned int PsdReader::TellFile(HANDLE file) {
    unsigned int ret = ::SetFilePointer(file, 0, NULL, FILE_CURRENT);
    ThrowIf(ret == INVALID_SET_FILE_POINTER);
    return ret;
}

// Scale 16-bit values to 8-bit
unsigned char PsdReader::Scale16To8(unsigned short value) {
    return static_cast<unsigned char>((value * 255 + 32768) / 65535);
}

CJPEGImage* PsdReader::ReadImage(LPCTSTR strFileName, bool& bOutOfMemory) {
    HANDLE hFile = nullptr;
    hFile = ::CreateFile(strFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    char* pBuffer = nullptr;
    void* pPixelData = nullptr;
    void* pEXIFData = nullptr;
    char* pICCProfile = nullptr;
    void* transform = nullptr;
    CJPEGImage* Image = nullptr;
    unsigned int nICCProfileSize = 0;

    try {
        const long long nFileSize = Helpers::GetFileSize(hFile);
        ThrowIf(nFileSize > MAX_PSD_FILE_SIZE);

        // Skip file signature
        SeekFile(hFile, 4);

        // Read version: 1 for PSD, 2 for PSB
        const unsigned short nVersion = ReadUShortFromFile(hFile);
        ThrowIf(nVersion != 1 && nVersion != 2);

        // Check reserved bytes
        char pReserved[6];
        ReadFromFile(pReserved, hFile, 6);
        ThrowIf(memcmp(pReserved, "\0\0\0\0\0\0", 6));

        // Read number of channels
        const unsigned short nRealChannels = ReadUShortFromFile(hFile);

        // Read width and height
        const unsigned int nHeight = ReadUIntFromFile(hFile);
        const unsigned int nWidth = ReadUIntFromFile(hFile);
        if (static_cast<double>(nHeight) * nWidth > MAX_IMAGE_PIXELS) {
            bOutOfMemory = true;
        }
        ThrowIf(bOutOfMemory || max(nHeight, nWidth) > MAX_IMAGE_DIMENSION || !min(nHeight, nWidth));

        // PSD usually have bit depths of 1, 8, 16, 32
        // Support 1-bit, 8-bit and 16-bit
        const unsigned short nBitDepth = ReadUShortFromFile(hFile);
        ThrowIf(nBitDepth != 1 && nBitDepth != 8 && nBitDepth != 16);

        // Read color mode and determine channels
        // Bitmap = 0; Grayscale = 1; Indexed = 2; RGB = 3; CMYK = 4; Multichannel = 7; Duotone = 8; Lab = 9.
        const unsigned short nColorMode = ReadUShortFromFile(hFile);
        unsigned short nChannels = 0;

        if(nBitDepth == 1 || nColorMode == MODE_Bitmap) {
            nChannels = 1;
        } else {
            switch (nColorMode) {
            case MODE_Grayscale:
            case MODE_Duotone:
                nChannels = min(nRealChannels, 1);
                break;
            case MODE_Multichannel:
                nChannels = min(nRealChannels, 3);
                break;
            case MODE_Lab:
            case MODE_RGB:
            case MODE_CMYK:
                nChannels = min(nRealChannels, 4);
                break;
            }
        }
        if (nChannels == 2) {
            nChannels = 1;
        }
        ThrowIf(nChannels != 1 && nChannels != 3 && nChannels != 4);

        // Skip color mode data
        const unsigned int nColorDataSize = ReadUIntFromFile(hFile);
        SeekFile(hFile, nColorDataSize);

        // Read resource section size and parse resources
        const unsigned int nResourceSectionSize = ReadUIntFromFile(hFile);
        bool bUseAlpha = nChannels == 4; // Default alpha detection

        ParseImageResources(hFile, pEXIFData, pICCProfile, nICCProfileSize, bUseAlpha, nColorMode);

        // Go back to start of file after resources
        SeekFileFromStart(hFile, PSD_HEADER_SIZE + 4 + nColorDataSize + 4 + nResourceSectionSize);

        // Skip Layer and Mask Info section
        unsigned long long nLayerSize = (nVersion == 2) ? ReadUInt64FromFile(hFile) : ReadUIntFromFile(hFile);
        const unsigned char nLayerSizeBytes = 4 * nVersion;
        SeekFile(hFile, nLayerSizeBytes);

        const short nLayerCount = ReadUShortFromFile(hFile);
        bUseAlpha = bUseAlpha && (nLayerCount <= 0);
        SeekFile(hFile, nLayerSize - 2 - nLayerSizeBytes);

        // Read compression method
        const unsigned short nCompressionMethod = ReadUShortFromFile(hFile);
        ThrowIf(nCompressionMethod != COMPRESSION_RLE && nCompressionMethod != COMPRESSION_None);

        // Read image data
        const unsigned int nImageDataSize = nFileSize - TellFile(hFile);
        pBuffer = new(std::nothrow) char[nImageDataSize];
        if (pBuffer == nullptr) {
            bOutOfMemory = true;
            ThrowIf(true);
        }
        ReadFromFile(pBuffer, hFile, nImageDataSize);

        if (nBitDepth == 1 || nColorMode == MODE_Bitmap) {
            // Calculate buffer sizes
            const int nOutputRowSize = Helpers::DoPadding(nWidth * nChannels, 4);

            // Allocate output buffer
            pPixelData = new(std::nothrow) char[nOutputRowSize * nHeight];
            if (pPixelData == nullptr) {
                bOutOfMemory = true;
                ThrowIf(true);
            }

            // Process pixel data based on compression method
            if (nCompressionMethod == COMPRESSION_RLE) {
                ProcessBitmapRLE(reinterpret_cast<unsigned char*>(pBuffer), nImageDataSize,
                    pPixelData, nWidth, nHeight, nOutputRowSize, nCompressionMethod, nVersion);
            } else {
                ProcessBitmapUncompressed(reinterpret_cast<unsigned char*>(pBuffer), nImageDataSize,
                    pPixelData, nWidth, nHeight, nOutputRowSize, nCompressionMethod, nVersion);
            }

            Image = new CJPEGImage(nWidth, nHeight, pPixelData, pEXIFData, nChannels, 0, IF_PSD, false, 0, 1, 0);
        } else {
            // Adjust channels if no alpha
            if (!bUseAlpha && nColorMode != MODE_CMYK) {
                nChannels = min(nChannels, 3);
            }

            // Apply ICC Profile
            if (nChannels == 3 || nChannels == 4) {
                if (nColorMode == MODE_Lab) {
                    transform = ICCProfileTransform::CreateLabTransform(nChannels == 4 ? ICCProfileTransform::FORMAT_LabA : ICCProfileTransform::FORMAT_Lab);
                    if (transform == NULL) {
                        // If we can't convert Lab to sRGB then just use the Lightness channel as grayscale
                        nChannels = min(nChannels, 1);
                    }
                }
                else if (nColorMode == MODE_RGB) {
                    transform = ICCProfileTransform::CreateTransform(pICCProfile, nICCProfileSize, nChannels == 4 ? ICCProfileTransform::FORMAT_BGRA : ICCProfileTransform::FORMAT_BGR);
                }
            }

            // Calculate buffer sizes
            const int nBytesPerUnit = (nBitDepth == 16) ? 2 : 1;
            // const int nRowSize = Helpers::DoPadding(nWidth * nChannels * nBytesPerUnit, 4);
            const int nOutputRowSize = Helpers::DoPadding(nWidth * nChannels, 4);

            // Allocate output buffer
            pPixelData = new(std::nothrow) char[nOutputRowSize * nHeight];
            if (pPixelData == nullptr) {
                bOutOfMemory = true;
                ThrowIf(true);
            }

            // Process pixel data based on compression method
            // TODO: better non-RGB support
            if (nCompressionMethod == COMPRESSION_RLE) {
                ProcessRLEData(reinterpret_cast<unsigned char*>(pBuffer), nImageDataSize,
                    pPixelData, nWidth, nHeight, nChannels, nOutputRowSize,
                    nColorMode, nBitDepth, nRealChannels, nVersion);
            } else {
                ProcessUncompressedData(reinterpret_cast<unsigned char*>(pBuffer), nImageDataSize,
                    pPixelData, nWidth, nHeight, nChannels, nOutputRowSize,
                    nColorMode, nBitDepth);
            }

            ICCProfileTransform::DoTransform(transform, pPixelData, pPixelData, nWidth, nHeight, nOutputRowSize);

            // Process alpha channel if present
            if (nChannels == 4) {
                uint32* pImage32 = static_cast<uint32*>(pPixelData);
                const COLORREF backgroundColor = (nColorMode == MODE_CMYK) ? 0 : CSettingsProvider::This().ColorTransparency();

                for (int i = 0; i < nWidth * nHeight; i++) {
                    pImage32[i] = Helpers::AlphaBlendBackground(pImage32[i], backgroundColor);
                }
            }

            Image = new CJPEGImage(nWidth, nHeight, pPixelData, pEXIFData, nChannels, 0, IF_PSD, false, 0, 1, 0);
        }
    } catch (...) {
        delete Image;
        Image = nullptr;
    }

    ::CloseHandle(hFile);
    if (Image == nullptr) {
        delete[] pPixelData;
    }
    delete[] pBuffer;
    delete[] pEXIFData;
    delete[] pICCProfile;
    ICCProfileTransform::DeleteTransform(transform);

    return Image;
}

bool PsdReader::ParseImageResources(HANDLE hFile, void*& pEXIFData, char*& pICCProfile,
    unsigned int& nICCProfileSize, bool& bUseAlpha, const int nColorMode){
    while (true) {
        try {
            if (ReadUIntFromFile(hFile) != RESOURCE_SIGNATURE) {
                break;
            }
        } catch (...) {
            break;
        }

        const unsigned short nResourceID = ReadUShortFromFile(hFile);

        // Skip Pascal string
        const unsigned char nStringSize = ReadUCharFromFile(hFile);
        SeekFile(hFile, nStringSize | 1);

        const unsigned int nResourceSize = ReadUIntFromFile(hFile);

        // Parse specific resources
        switch (nResourceID) {
        case RESOURCE_ICC_PROFILE: // ICC Profile
            if (nColorMode == MODE_RGB) {
                pICCProfile = new(std::nothrow) char[nResourceSize];
            }
            if (pICCProfile != NULL) {
                ReadFromFile(pICCProfile, hFile, nResourceSize);
                SeekFile(hFile, -nResourceSize);
                nICCProfileSize = nResourceSize;
            }
            break;

        case RESOURCE_ALPHA_IDENTIFIERS: // Alpha Identifiers
            if (bUseAlpha) {
                bUseAlpha = false;
                int i = 0;
                for (i = 0; i < nResourceSize / 4; i++) {
                    if (ReadUIntFromFile(hFile) == 0) {
                        bUseAlpha = true;
                        break;
                    }
                }
                SeekFile(hFile, -i * 4);
            }
            break;

        case RESOURCE_VERSION_INFO: // Version Info
            if (nResourceSize >= 5) {
                ReadUIntFromFile(hFile);
                ThrowIf(!ReadUCharFromFile(hFile));
                SeekFile(hFile, -5);
            }
            break;

        case RESOURCE_EXIF_DATA_1: // EXIF data 1
        case RESOURCE_EXIF_DATA_3: // EXIF data 3
            if (pEXIFData == nullptr && nResourceSize < 65526) {
                pEXIFData = new(std::nothrow) char[nResourceSize + 10];
                if (pEXIFData != nullptr) {
                    memcpy(pEXIFData, "\xFF\xE1\0\0Exif\0\0", 10);
                    *((unsigned short*)pEXIFData + 1) = _byteswap_ushort(nResourceSize + 8);
                    ReadFromFile(static_cast<char*>(pEXIFData) + 10, hFile, nResourceSize);
                    SeekFile(hFile, -nResourceSize);
                }
            }
            break;
        }

        // Skip resource data
        SeekFile(hFile, (nResourceSize + 1) & -2);
    }

    return true;
}

void PsdReader::ProcessBitmapRLE(const unsigned char* pBuffer, unsigned int nImageDataSize,
    void* pPixelData, unsigned int nWidth, unsigned int nHeight,
    unsigned int nOutputRowSize, unsigned short nCompressionMethod,
    unsigned short nVersion) {
    unsigned char* pOutput = static_cast<unsigned char*>(pPixelData);

    // Skip byte counts for scanlines
    const unsigned char* pOffset = pBuffer;
    if (nVersion == 2) {
        pOffset += nHeight * 4;
    }
    else {
        pOffset += nHeight * 2;
    }

    for (unsigned int row = 0; row < nHeight; row++) {
        unsigned char* pRow = pOutput + row * nOutputRowSize;
        const unsigned char* p = pOffset;
        unsigned int count = 0;

        unsigned int currentBit = 0;

        while (count < nWidth) {
            if (p >= pBuffer + nImageDataSize) {
                ThrowIf(true);
                return;
            }

            unsigned char c = *p++;
            unsigned int runLength;

            if (c > 128) {
                runLength = 256 - c + 1;
                if (p >= pBuffer + nImageDataSize) {
                    ThrowIf(true);
                    return;
                }
                unsigned char value = *p++;

                for (unsigned int i = 0; i < runLength && count < nWidth; i++) {
                    for (int bit = 7; bit >= 0 && count < nWidth; bit--) {
                        unsigned char bitValue = (value >> bit) & 1;
                        pRow[count] = bitValue ? 0 : 255;
                        count++;
                    }
                }
            } else if (c < 128) {
                runLength = c + 1;

                for (unsigned int i = 0; i < runLength && count < nWidth; i++) {
                    if (p >= pBuffer + nImageDataSize) {
                        ThrowIf(true);
                        return;
                    }
                    unsigned char value = *p++;

                    for (int bit = 7; bit >= 0 && count < nWidth; bit--) {
                        unsigned char bitValue = (value >> bit) & 1;
                        pRow[count] = bitValue ? 0 : 255;
                        count++;
                    }
                }
            } else {
                // c == 128
                continue;
            }
        }

        // Update offset for next row
        if (nVersion == 2) {
            unsigned int rowLength = _byteswap_ulong(*reinterpret_cast<const unsigned int*>(pBuffer + row * 4));
            pOffset += rowLength;
        } else {
            unsigned short rowLength = _byteswap_ushort(*reinterpret_cast<const unsigned short*>(pBuffer + row * 2));
            pOffset += rowLength;
        }
    }
}

void PsdReader::ProcessBitmapUncompressed(const unsigned char* pBuffer, unsigned int nImageDataSize,
    void* pPixelData, unsigned int nWidth, unsigned int nHeight,
    unsigned int nOutputRowSize, unsigned short nCompressionMethod,
    unsigned short nVersion) {
    unsigned char* pOutput = static_cast<unsigned char*>(pPixelData);
    const unsigned char* pInput = pBuffer;

    const unsigned int nBytesPerRow = (nWidth + 7) / 8;

    if (nBytesPerRow * nHeight > nImageDataSize) {
        ThrowIf(true);
        return;
    }

    for (unsigned int row = 0; row < nHeight; row++) {
        unsigned char* pRow = pOutput + row * nOutputRowSize;
        const unsigned char* pSrc = pInput + row * nBytesPerRow;

        for (unsigned int col = 0; col < nWidth; col++) {
            unsigned int byteIndex = col / 8;
            unsigned int bitIndex = 7 - (col % 8); // MSB
            unsigned char bitValue = (pSrc[byteIndex] >> bitIndex) & 1;

            pRow[col] = bitValue ? 0 : 255;
        }
    }
}

void PsdReader::ProcessRLEData(const unsigned char* pBuffer, unsigned int nImageDataSize,
    void* pPixelData, unsigned int nWidth, unsigned int nHeight,
    unsigned int nChannels, unsigned int nOutputRowSize,
    unsigned short nColorMode, unsigned short nBitDepth,
    unsigned short nRealChannels, unsigned short nVersion) {
    // Skip byte counts for scanlines
    const unsigned char* p = pBuffer + nHeight * nRealChannels * 2 * nVersion;
    const unsigned char* pOffset = p;

    for (unsigned channel = 0; channel < nChannels; channel++) {
        // Calculate target channel
        const unsigned rchannel = (nColorMode == MODE_Lab) ? channel : (-channel - 2) % nChannels;

        for (unsigned row = 0; row < nHeight; row++) {
            p = pOffset;
            unsigned count = 0;

            if (nBitDepth == 8) {
                // 8-bit RLE decompression
                while (count < nWidth) {
                    ThrowIf(p >= pBuffer + nImageDataSize);
                    unsigned char c = *p++;

                    if (c > 128) {
                        c = ~c + 2;
                        ThrowIf(p >= pBuffer + nImageDataSize);
                        const unsigned char value = *p++;

                        for (unsigned i = 0; i < c; i++) {
                            unsigned char* pixel = static_cast<unsigned char*>(pPixelData) +
                                row * nOutputRowSize + (count + i) * nChannels + rchannel;
                            ThrowIf(pixel >= static_cast<unsigned char*>(pPixelData) + nOutputRowSize * nHeight);
                            *pixel = value;
                        }
                    } else if (c < 128) {
                        c++;
                        for (unsigned i = 0; i < c; i++) {
                            ThrowIf(p >= pBuffer + nImageDataSize);
                            const unsigned char value = *p++;

                            unsigned char* pixel = static_cast<unsigned char*>(pPixelData) +
                                row * nOutputRowSize + (count + i) * nChannels + rchannel;
                            ThrowIf(pixel >= static_cast<unsigned char*>(pPixelData) + nOutputRowSize * nHeight);
                            *pixel = value;
                        }
                    }
                    count += c;
                }
            } else {
                // 16-bit RLE decompression
                while (count < nWidth) {
                    ThrowIf(p >= pBuffer + nImageDataSize);
                    unsigned char c = *p++;

                    if (c > 128) {
                        c = ~c + 2;
                        ThrowIf(p + 1 >= pBuffer + nImageDataSize);
                        const unsigned short value = (p[0] << 8) | p[1];
                        p += 2;

                        for (unsigned i = 0; i < c; i++) {
                            unsigned char* pixel = static_cast<unsigned char*>(pPixelData) +
                                row * nOutputRowSize + (count + i) * nChannels + rchannel;
                            ThrowIf(pixel >= static_cast<unsigned char*>(pPixelData) + nOutputRowSize * nHeight);
                            *pixel = Scale16To8(value);
                        }
                    } else if (c < 128) {
                        c++;
                        for (unsigned i = 0; i < c; i++) {
                            ThrowIf(p + 1 >= pBuffer + nImageDataSize);
                            const unsigned short value = (p[0] << 8) | p[1];
                            p += 2;

                            unsigned char* pixel = static_cast<unsigned char*>(pPixelData) +
                                row * nOutputRowSize + (count + i) * nChannels + rchannel;
                            ThrowIf(pixel >= static_cast<unsigned char*>(pPixelData) + nOutputRowSize * nHeight);
                            *pixel = Scale16To8(value);
                        }
                    }
                    count += c;
                }
            }

            // Update offset for next row
            if (nVersion == 2) {
                pOffset += _byteswap_ulong(*reinterpret_cast<const unsigned int*>(
                    pBuffer + (channel * nHeight + row) * 4));
            } else {
                pOffset += _byteswap_ushort(*reinterpret_cast<const unsigned short*>(
                    pBuffer + (channel * nHeight + row) * 2));
            }
        }
    }
}

void PsdReader::ProcessUncompressedData(const unsigned char* pBuffer, unsigned int nImageDataSize,
    void* pPixelData, unsigned int nWidth, unsigned int nHeight,
    unsigned int nChannels, unsigned int nOutputRowSize,
    unsigned short nColorMode, unsigned short nBitDepth) {
    const unsigned char* p = pBuffer;

    for (unsigned channel = 0; channel < nChannels; channel++) {
        const unsigned rchannel = (nColorMode == MODE_Lab) ? channel : (-channel - 2) % nChannels;

        for (unsigned row = 0; row < nHeight; row++) {
            for (unsigned col = 0; col < nWidth; col++) {
                if (nBitDepth == 8) {
                    ThrowIf(p >= pBuffer + nImageDataSize);
                    const unsigned char value = *p++;

                    unsigned char* pixel = static_cast<unsigned char*>(pPixelData) +
                        row * nOutputRowSize + col * nChannels + rchannel;
                    ThrowIf(pixel >= static_cast<unsigned char*>(pPixelData) + nOutputRowSize * nHeight);
                    *pixel = value;
                } else {
                    ThrowIf(p + 1 >= pBuffer + nImageDataSize);
                    const unsigned short value = (p[0] << 8) | p[1];
                    p += 2;

                    unsigned char* pixel = static_cast<unsigned char*>(pPixelData) +
                        row * nOutputRowSize + col * nChannels + rchannel;
                    ThrowIf(pixel >= static_cast<unsigned char*>(pPixelData) + nOutputRowSize * nHeight);
                    *pixel = Scale16To8(value);
                }
            }
        }
    }
}

CJPEGImage* PsdReader::ReadThumb(LPCTSTR strFileName, bool& bOutOfMemory) {
    HANDLE hFile = ::CreateFile(strFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    char* pBuffer = nullptr;
    void* pPixelData = nullptr;
    void* pEXIFData = nullptr;
    CJPEGImage* Image = nullptr;
    int nWidth = 0, nHeight = 0, nChannels = 0, nJpegSize = 0;
    TJSAMP eChromoSubSampling;

    try {
        // Skip file header and color mode data
        SeekFile(hFile, PSD_HEADER_SIZE);
        const unsigned int nColorDataSize = ReadUIntFromFile(hFile);
        SeekFile(hFile, nColorDataSize);

        // Skip resource section size
        ReadUIntFromFile(hFile);

        // Parse thumbnail resources
        ParseThumbnailResources(hFile, pEXIFData, pBuffer, nJpegSize, pPixelData,
            nWidth, nHeight, nChannels, eChromoSubSampling, bOutOfMemory);

        if (pPixelData != nullptr) {
            Image = new CJPEGImage(nWidth, nHeight, pPixelData, pEXIFData,
                nChannels, Helpers::CalculateJPEGFileHash(pBuffer, nJpegSize), IF_JPEG_Embedded, false, 0, 1, 0);
            Image->SetJPEGComment(Helpers::GetJPEGComment(pBuffer, nJpegSize));
            Image->SetJPEGChromoSampling(eChromoSubSampling);
        }
    } catch (...) {
        delete Image;
        Image = nullptr;
    }

    ::CloseHandle(hFile);
    if (Image == nullptr) {
        delete[] pPixelData;
    }
    delete[] pEXIFData;
    delete[] pBuffer;

    return Image;
}

bool PsdReader::ParseThumbnailResources(HANDLE hFile, void*& pEXIFData, char*& pBuffer,
    int& nJpegSize, void*& pPixelData, int& nWidth,
    int& nHeight, int& nChannels, TJSAMP& eChromoSubSampling,
    bool& bOutOfMemory) {
    while (true) {
        try {
            if (ReadUIntFromFile(hFile) != RESOURCE_SIGNATURE) { // "8BIM"
                break;
            }
        } catch (...) {
            break;
        }

        const unsigned short nResourceID = ReadUShortFromFile(hFile);

        // Skip Pascal string
        const unsigned char nStringSize = ReadUCharFromFile(hFile);
        SeekFile(hFile, nStringSize | 1);

        const unsigned int nResourceSize = ReadUIntFromFile(hFile);

        switch (nResourceID) {
        case RESOURCE_THUMBNAIL_4: // Photoshop 4.0 Thumbnail
        case RESOURCE_THUMBNAIL_5: // Photoshop 5.0 Thumbnail
            // Skip thumbnail resource header
            SeekFile(hFile, THUMBNAIL_HEADER_SIZE);

            // Read embedded JPEG thumbnail
            nJpegSize = nResourceSize - THUMBNAIL_HEADER_SIZE;
            if (nJpegSize > MAX_JPEG_FILE_SIZE) {
                bOutOfMemory = true;
                ThrowIf(true);
            }

            pBuffer = new(std::nothrow) char[nJpegSize];
            if (pBuffer == nullptr) {
                bOutOfMemory = true;
                ThrowIf(true);
            }

            ReadFromFile(pBuffer, hFile, nJpegSize);
            SeekFile(hFile, -nResourceSize);

            pPixelData = TurboJpeg::ReadImage(nWidth, nHeight, nChannels,
                eChromoSubSampling, bOutOfMemory,
                pBuffer, nJpegSize);
            break;

        case RESOURCE_EXIF_DATA_1: // EXIF data 1
        case RESOURCE_EXIF_DATA_3: // EXIF data 3
            if (pEXIFData == nullptr && nResourceSize < 65526) {
                pEXIFData = new(std::nothrow) char[nResourceSize + 10];
                if (pEXIFData != nullptr) {
                    memcpy(pEXIFData, "\xFF\xE1\0\0Exif\0\0", 10);
                    *reinterpret_cast<unsigned short*>(static_cast<char*>(pEXIFData) + 1) =
                        _byteswap_ushort(nResourceSize + 8);
                    ReadFromFile(static_cast<char*>(pEXIFData) + 10, hFile, nResourceSize);
                    SeekFile(hFile, -nResourceSize);
                }
            }
            break;
        }

        // Skip resource data
        SeekFile(hFile, (nResourceSize + 1) & -2);
    }

    return (pPixelData != nullptr);
}
