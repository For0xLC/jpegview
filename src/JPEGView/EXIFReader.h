#pragma once

// Signed rational number: numerator/denominator
class SignedRational {
public:
	SignedRational(int num, int denom) { Numerator = num; Denominator = denom; }
	int Numerator;
	int Denominator;
};

// Unsigned rational number: numerator/denominator
class Rational {
public:
	Rational(unsigned int num, unsigned int denom) { Numerator = num; Denominator = denom; }
	unsigned int Numerator;
	unsigned int Denominator;
};

class GPSCoordinate {
public:
	GPSCoordinate(LPCTSTR reference, double degrees, double minutes, double seconds) {
		m_sReference = CString(reference);
		if (minutes == 0.0 && seconds == 0.0) {
			minutes = 60 * abs(degrees - (int)degrees);
			degrees = (int)degrees;
		}
		if (seconds == 0.0) {
			seconds = 60 * abs(minutes - (int)minutes);
			minutes = (int)minutes;
		}
		Degrees = degrees;
		Minutes = minutes;
		Seconds = seconds;
	}
	LPCTSTR GetReference() { return m_sReference; }
	double Degrees;
	double Minutes;
	double Seconds;
private:
	CString m_sReference;
};

// Reads and parses the EXIF data of JPEG images
class CEXIFReader {
public:
	// The pApp1Block must point to the APP1 block of the EXIF data, including the APP1 block marker
	// The class does not take ownership of the memory (no copy made), thus the APP1 block must not be deleted
	// while the EXIF reader class is deleted.
	CEXIFReader(void* pApp1Block, EImageFormat eImageFormat);
	~CEXIFReader(void);

	// Parse date string in the EXIF date/time format
	static bool ParseDateString(SYSTEMTIME & date, const CString& str);

public:
	// Camera model, image comment and description. The returned pointers are valid while the EXIF reader is not deleted.
	LPCTSTR GetCameraModel() { return m_sModel; }
	LPCTSTR GetUserComment() { return m_sUserComment; }
	LPCTSTR GetImageDescription() { return m_sImageDescription; }
	LPCTSTR GetSoftware() { return m_sSoftware; }
	LPCTSTR GetXPComment() { return m_sXPComment; }
	bool GetCameraModelPresent() { return !m_sModel.IsEmpty(); }
	bool GetSoftwarePresent() { return !m_sSoftware.IsEmpty(); }
	bool GetXPCommentPresent() { return !m_sXPComment.IsEmpty(); }
	// Date-time the picture was taken
	const SYSTEMTIME& GetAcquisitionTime() { return m_acqDate; }
	bool GetAcquisitionTimePresent() { return m_acqDate.wYear > 1600; }
	// Date-time the picture was saved/modified (used by editing software)
	const SYSTEMTIME& GetDateTime() { return m_dateTime; }
	bool GetDateTimePresent() { return m_dateTime.wYear > 1600; }
	// Exposure time
	const Rational& GetExposureTime() { return m_exposureTime; }
	bool GetExposureTimePresent() { return m_exposureTime.Denominator != 0; }
	// Exposure bias
	double GetExposureBias() { return m_dExposureBias; }
	bool GetExposureBiasPresent() { return m_dExposureBias != UNKNOWN_DOUBLE_VALUE; }
	// Exposure program (0 = Not defined, 1 = Manual, 2 = Normal program, 3 = Aperture priority, 4 = Shutter priority, 5 = Creative program, 6 = Action program, 7 = Portrait mode, 8 = Landscape mode)
	int GetExposureProgram() { return m_nExposureProgram; }
	bool GetExposureProgramPresent() { return m_nExposureProgram > 0; }
	// Metering mode (0 = Unknown, 1 = Average, 2 = CenterWeightedAverage, 3 = Spot, 4 = MultiSpot, 5 = Pattern, 6 = Partial, 255 = other)
	int GetMeteringMode() { return m_nMeteringMode; }
	bool GetMeteringModePresent() { return m_nMeteringMode > 0; }
	// White balance (0 = Auto, 1 = Manual)
	int GetWhiteBalance() { return m_nWhiteBalance; }
	bool GetWhiteBalancePresent() { return m_nWhiteBalance > 0; }
	// Lens model
	LPCTSTR GetLensModel() { return m_sLensModel; }
	bool GetLensModelPresent() { return !m_sLensModel.IsEmpty(); }
	// Scene capture type (0 = Standard, 1 = Landscape, 2 = Portrait, 3 = Night scene)
	int GetSceneCaptureType() { return m_nSceneCaptureType; }
	bool GetSceneCaptureTypePresent() { return m_nSceneCaptureType > 0; }
	// Flag if flash fired
	bool GetFlashFired() { return m_bFlashFired; }
	bool GetFlashFiredPresent() { return m_bFlashFlagPresent; }
	// Focal length (mm)
	double GetFocalLength() { return m_dFocalLength; }
	bool GetFocalLengthPresent() { return m_dFocalLength != UNKNOWN_DOUBLE_VALUE; }
	// F-Number
	double GetFNumber() { return m_dFNumber; }
	bool GetFNumberPresent() { return m_dFNumber != UNKNOWN_DOUBLE_VALUE; }
	// ISO speed value
	int GetISOSpeed() { return m_nISOSpeed; }
	bool GetISOSpeedPresent() { return m_nISOSpeed > 0; }
	// Image orientation as detected by sensor, coding according EXIF standard (thus no angle in degrees)
	int GetImageOrientation() { return m_nImageOrientation; }
	bool ImageOrientationPresent() { return m_nImageOrientation > 0; }
	// Thumbnail image information
	bool HasJPEGCompressedThumbnail() { return m_bHasJPEGCompressedThumbnail; }
	int GetJPEGThumbStreamLen() { return m_nJPEGThumbStreamLen; }
	int GetThumbnailWidth() { return m_nThumbWidth; }
	int GetThumbnailHeight() { return m_nThumbHeight; }
	// GPS information
	bool IsGPSInformationPresent() { return m_pLatitude != NULL && m_pLongitude != NULL; }
	bool IsGPSAltitudePresent() { return m_dAltitude != UNKNOWN_DOUBLE_VALUE; }
	GPSCoordinate* GetGPSLatitude() { return m_pLatitude; }
	GPSCoordinate* GetGPSLongitude() { return m_pLongitude; }
	double GetGPSAltitude() { return m_dAltitude; }

	// Sets the image orientation to given value (if tag was present in input stream).
	// Writes to the APP1 block passed in constructor.
	void WriteImageOrientation(int nOrientation);
	
	// Updates an existing JPEG compressed thumbnail image by given JPEG stream (SOI stripped)
	// Writes to the APP1 block passed in constructor. Make sure that enough memory is allocated for this APP1
	// block to hold the additional thumbnail data.
	void UpdateJPEGThumbnail(unsigned char* pJPEGStream, int nStreamLen, int nEXIFBlockLenCorrection, CSize sizeThumb);

	// Delete the thumbnail image
	// Writes to the APP1 block passed in constructor.
	void DeleteThumbnail();
public:
	// unknown double value
	static double UNKNOWN_DOUBLE_VALUE;

private:
	CString m_sModel;
	CString m_sUserComment;
	CString m_sImageDescription;
	CString m_sSoftware;
	CString m_sXPComment;
	SYSTEMTIME m_acqDate;
	SYSTEMTIME m_dateTime;
	Rational m_exposureTime;
	double m_dExposureBias;
	bool m_bFlashFired;
	bool m_bFlashFlagPresent;
	double m_dFocalLength;
	double m_dFNumber;
	int m_nISOSpeed;
	int m_nImageOrientation;
	int m_nExposureProgram;
	int m_nMeteringMode;
	int m_nWhiteBalance;
	CString m_sLensModel;
	int m_nSceneCaptureType;
	bool m_bHasJPEGCompressedThumbnail;
	int m_nThumbWidth;
	int m_nThumbHeight;
	int m_nJPEGThumbStreamLen;
	GPSCoordinate* m_pLatitude;
	GPSCoordinate* m_pLongitude;
	double m_dAltitude;

	bool m_bLittleEndian;
	uint8* m_pApp1;
	uint8* m_pTagOrientation;
	uint8* m_pLastIFD0;
	uint8* m_pIFD1;
	uint8* m_pLastIFD1;

	void ReadGPSData(uint8* pTIFFHeader, uint8* pTagGPSIFD, int nApp1Size, bool bLittleEndian);
	GPSCoordinate* ReadGPSCoordinate(uint8* pTIFFHeader, uint8* pTagLatOrLong, LPCTSTR reference, bool bLittleEndian);
};
