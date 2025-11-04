#pragma once

#include "JPEGImage.h"

class PsdReader
{
public:
    // Returns image from PSD file
    static CJPEGImage* ReadImage(LPCTSTR strFileName, bool& bOutOfMemory);

    // Returns embedded JPEG thumbnail from PSD file
    static CJPEGImage* ReadThumb(LPCTSTR strFileName, bool& bOutOfMemory);

private:
    enum ColorMode {
        MODE_Bitmap = 0,
        MODE_Grayscale = 1,
        MODE_Indexed = 2,
        MODE_RGB = 3,
        MODE_CMYK = 4,
        MODE_Multichannel = 7,
        MODE_Duotone = 8,
        MODE_Lab = 9,
    };

    enum CompressionMode {
        COMPRESSION_None = 0,
        COMPRESSION_RLE = 1,
        COMPRESSION_ZipWithoutPrediction = 2,
        COMPRESSION_ZipWithPrediction = 3,
    };

    // Resource constants
    static constexpr auto RESOURCE_SIGNATURE = 0x3842494D; // "8BIM"
    static constexpr auto RESOURCE_ICC_PROFILE = 0x040F;
    static constexpr auto RESOURCE_ALPHA_IDENTIFIERS = 0x041D;
    static constexpr auto RESOURCE_VERSION_INFO = 0x0421;
    static constexpr auto RESOURCE_EXIF_DATA_1 = 0x0422;
    static constexpr auto RESOURCE_EXIF_DATA_3 = 0x0423;
    static constexpr auto RESOURCE_THUMBNAIL_4 = 0x0409;
    static constexpr auto RESOURCE_THUMBNAIL_5 = 0x040C;

    // File structure constants
    static constexpr auto PSD_HEADER_SIZE = 26;
    static constexpr auto THUMBNAIL_HEADER_SIZE = 28;

    static inline void ThrowIf(bool bShouldThrow);

    // File reading helpers
    static inline void ReadFromFile(void* dst, HANDLE file, DWORD sz);
    static inline unsigned long long ReadUInt64FromFile(HANDLE file);
    static inline unsigned int ReadUIntFromFile(HANDLE file);
    static inline unsigned short ReadUShortFromFile(HANDLE file);
    static inline unsigned char ReadUCharFromFile(HANDLE file);
    static inline void SeekFile(HANDLE file, LONG offset);
    static inline void SeekFileFromStart(HANDLE file, LONG offset);
    static inline unsigned int TellFile(HANDLE file);

    // Data processing helpers
    static inline unsigned char Scale16To8(unsigned short value);

    // Image parsing helpers
    static bool ParseImageResources(HANDLE hFile, void*& pEXIFData, char*& pICCProfile,
        unsigned int& nICCProfileSize, bool& bUseAlpha, const int nColorMode);
    static bool ParseThumbnailResources(HANDLE hFile, void*& pEXIFData, char*& pBuffer,
        int& nJpegSize, void*& pPixelData, int& nWidth,
        int& nHeight, int& nChannels, TJSAMP& eChromoSubSampling,
        bool& bOutOfMemory);

    // Pixel data processing
    static void ProcessBitmapRLE(const unsigned char* pBuffer, unsigned int nImageDataSize,
        void* pPixelData, unsigned int nWidth, unsigned int nHeight,
        unsigned int nOutputRowSize, unsigned short nCompressionMethod,
        unsigned short nVersion);

    static void ProcessBitmapUncompressed(const unsigned char* pBuffer, unsigned int nImageDataSize,
        void* pPixelData, unsigned int nWidth, unsigned int nHeight,
        unsigned int nOutputRowSize, unsigned short nCompressionMethod,
        unsigned short nVersion);

    static void ProcessRLEData(const unsigned char* pBuffer, unsigned int nImageDataSize,
        void* pPixelData, unsigned int nWidth, unsigned int nHeight,
        unsigned int nChannels, unsigned int nOutputRowSize,
        unsigned short nColorMode, unsigned short nBitDepth,
        unsigned short nRealChannels, unsigned short nVersion);

    static void ProcessUncompressedData(const unsigned char* pBuffer, unsigned int nImageDataSize,
        void* pPixelData, unsigned int nWidth, unsigned int nHeight,
        unsigned int nChannels, unsigned int nOutputRowSize,
        unsigned short nColorMode, unsigned short nBitDepth);
};
