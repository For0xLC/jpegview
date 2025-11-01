#include "StdAfx.h"
#include "resource.h"
#include "MainDlg.h"
#include "JPEGImage.h"
#include "EXIFDisplayCtl.h"
#include "EXIFDisplay.h"
#include "RawMetadata.h"
#include "SettingsProvider.h"
#include "HelpersGUI.h"
#include "NLS.h"

static int GetFileNameHeight(HDC dc) {
	CSize size;
	HelpersGUI::SelectDefaultFileNameFont(dc);
	::GetTextExtentPoint32(dc, _T("("), 1, &size);
	return size.cy;
}

static CString CreateGPSString(GPSCoordinate* latitude, GPSCoordinate* longitude) {
	const int BUFF_SIZE = 96;
	TCHAR buff[BUFF_SIZE];
	_stprintf_s(buff, BUFF_SIZE, _T("%s %.0f° %.0f' %.0f'' / %s %.0f° %.0f' %.0f''"),
		latitude->GetReference(), latitude->Degrees, latitude->Minutes, latitude->Seconds,
		longitude->GetReference(), longitude->Degrees, longitude->Minutes, longitude->Seconds);
	return CString(buff);
}

static CString CreateGPSURL(GPSCoordinate* latitude, GPSCoordinate* longitude) {
	double lng = longitude->Degrees + longitude->Minutes / 60 + longitude->Seconds / (60 * 60);
	if (_tcsicmp(longitude->GetReference(), _T("W")) == 0)
		lng = -lng;

	double lat = latitude->Degrees + latitude->Minutes / 60 + latitude->Seconds / (60 * 60);
	if (_tcsicmp(latitude->GetReference(), _T("S")) == 0)
		lat = -lat;

	CString mapProvider = CSettingsProvider::This().GPSMapProvider();

	const int BUFF_SIZE = 32;
	TCHAR buffLat[BUFF_SIZE];
	TCHAR buffLng[BUFF_SIZE];
	_stprintf_s(buffLat, BUFF_SIZE, _T("%.5f"), lat);
	_stprintf_s(buffLng, BUFF_SIZE, _T("%.5f"), lng);

	mapProvider.Replace(_T("{lat}"), buffLat);
	mapProvider.Replace(_T("{lng}"), buffLng);

	return mapProvider;
}

CEXIFDisplayCtl::CEXIFDisplayCtl(CMainDlg* pMainDlg, CPanel* pImageProcPanel) : CPanelController(pMainDlg, false) {
	m_bVisible = CSettingsProvider::This().ShowFileInfo();
	m_nFileNameHeight = 0;
	m_pImageProcPanel = pImageProcPanel;
	m_pPanel = m_pEXIFDisplay = new CEXIFDisplay(pMainDlg->m_hWnd, this);
	m_pEXIFDisplay->GetControl<CButtonCtrl*>(CEXIFDisplay::ID_btnShowHideHistogram)->SetButtonPressedHandler(&OnShowHistogram, this);
	CButtonCtrl* pCloseBtn = m_pEXIFDisplay->GetControl<CButtonCtrl*>(CEXIFDisplay::ID_btnClose);
	pCloseBtn->SetButtonPressedHandler(&OnClose, this);
	pCloseBtn->SetShow(false);
	m_pEXIFDisplay->SetShowHistogram(CSettingsProvider::This().ShowHistogram());
}

CEXIFDisplayCtl::~CEXIFDisplayCtl() {
	delete m_pEXIFDisplay;
	m_pEXIFDisplay = NULL;
}

bool CEXIFDisplayCtl::IsVisible() { 
	return CurrentImage() != NULL && m_bVisible; 
}

void CEXIFDisplayCtl::SetVisible(bool bVisible) {
	if (m_bVisible != bVisible) {
		m_bVisible = bVisible;
		InvalidateMainDlg();
	}
}

void CEXIFDisplayCtl::SetActive(bool bActive) {
	SetVisible(bActive);
}

void CEXIFDisplayCtl::AfterNewImageLoaded() {
	m_pEXIFDisplay->ClearTexts();
	m_pEXIFDisplay->SetHistogram(NULL);
}

void CEXIFDisplayCtl::OnPrePaintMainDlg(HDC hPaintDC) {
	if (m_pMainDlg->IsShowFileName() && m_nFileNameHeight == 0) {
		m_nFileNameHeight = GetFileNameHeight(hPaintDC);
	}
	m_pEXIFDisplay->SetPosition(CPoint(m_pImageProcPanel->PanelRect().left, m_pMainDlg->IsShowFileName() ? m_nFileNameHeight + 6 : 0));
	FillEXIFDataDisplay();
	if (CurrentImage() != NULL && m_pEXIFDisplay->GetShowHistogram()) {
		m_pEXIFDisplay->SetHistogram(CurrentImage()->GetProcessedHistogram());
	}
}

void CEXIFDisplayCtl::FillEXIFDataDisplay() {
	m_pEXIFDisplay->ClearTexts();

	m_pEXIFDisplay->SetHistogram(NULL);

	CString sPrefix, sFileTitle;
	LPCTSTR sCurrentFileName = m_pMainDlg->CurrentFileName(true);
	const CFileList* pFileList = m_pMainDlg->GetFileList();
	if (CurrentImage()->IsClipboardImage()) {
		sPrefix = sCurrentFileName;
	} else if (pFileList->Current() != NULL) {
		sPrefix.Format(_T("[%d/%d]"), pFileList->CurrentIndex() + 1, pFileList->Size());
		sFileTitle = sCurrentFileName;
		sFileTitle += Helpers::GetMultiframeIndex(m_pMainDlg->GetCurrentImage());
	}
	LPCTSTR sComment = NULL;
	m_pEXIFDisplay->AddPrefix(sPrefix);
	m_pEXIFDisplay->AddTitle(sFileTitle);
	m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Image width:")), CurrentImage()->OrigWidth());
	m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Image height:")), CurrentImage()->OrigHeight());

	size_t nPixel = (size_t)(CurrentImage()->OrigWidth() * CurrentImage()->OrigHeight());
	if (nPixel > 100000) {
		float nMegaPixel = nPixel / 1000000.0;
		CString sMegaPixel;
		sMegaPixel.Format(_T("%.1f %s"), nMegaPixel, _T("MP"));
		m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Pixels:")), sMegaPixel);
	}

	CString sFileSize;
	if (!CurrentImage()->IsClipboardImage() && pFileList->Current() != NULL) {
		HANDLE hFile = ::CreateFile(pFileList->Current(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			__int64 fileSize = 0;
			::GetFileSizeEx(hFile, (PLARGE_INTEGER)&fileSize);
			::CloseHandle(hFile);
			if (fileSize > 0) {
				const TCHAR* units[] = { _T("Bytes"), _T("KB"), _T("MB"), _T("GB") };
				double value = fileSize;
				int exponent = 0;
				while (value >= 1024 && exponent < sizeof(units) / sizeof(units[0]) - 1) {
					value /= 1024.0;
					exponent++;
				}
				sFileSize.Format(_T("%.1f %s"), value, units[exponent]);
			}
		}
	}
	m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Size:")), sFileSize);

	if (!CurrentImage()->IsClipboardImage()) {
		CEXIFReader* pEXIFReader = CurrentImage()->GetEXIFReader();
		CRawMetadata* pRawMetaData = CurrentImage()->GetRawMetadata();
		if (pEXIFReader != NULL) {
			sComment = pEXIFReader->GetUserComment();
			if (sComment == NULL || sComment[0] == 0 || ((std::wstring) sComment).find_first_not_of(L" \t\n\r\f\v", 0) == std::wstring::npos) {
				sComment = pEXIFReader->GetImageDescription();
			}
			if (pEXIFReader->GetAcquisitionTimePresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Acquisition date:")), pEXIFReader->GetAcquisitionTime());
			} else if (pEXIFReader->GetDateTimePresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exif Date Time:")), pEXIFReader->GetDateTime());
			} else {
				const FILETIME* pFileTime = pFileList->CurrentModificationTime();
				if (pFileTime != NULL) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modification date:")), *pFileTime);
				}
			}
			if (pEXIFReader->IsGPSInformationPresent()) {
				CString sGPSLocation = CreateGPSString(pEXIFReader->GetGPSLatitude(), pEXIFReader->GetGPSLongitude());
				m_pEXIFDisplay->SetGPSLocation(sGPSLocation, CreateGPSURL(pEXIFReader->GetGPSLatitude(), pEXIFReader->GetGPSLongitude()));
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Location:")), sGPSLocation, true);
				if (pEXIFReader->IsGPSAltitudePresent()) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Altitude (m):")), pEXIFReader->GetGPSAltitude(), 0);
				}
			}
			if (pEXIFReader->GetCameraModelPresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Camera model:")), pEXIFReader->GetCameraModel());
			}
			if (pEXIFReader->GetLensModelPresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Lens model:")), pEXIFReader->GetLensModel());
			}
			if (pEXIFReader->GetExposureTimePresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exposure time (s):")), pEXIFReader->GetExposureTime());
			}
			if (pEXIFReader->GetExposureProgramPresent()) {
				CString sExposureProgram;
				switch (pEXIFReader->GetExposureProgram()) {
				case 1: sExposureProgram = CNLS::GetString(_T("Manual")); break;
				case 2: sExposureProgram = CNLS::GetString(_T("Normal program")); break;
				case 3: sExposureProgram = CNLS::GetString(_T("Aperture priority")); break;
				case 4: sExposureProgram = CNLS::GetString(_T("Shutter priority")); break;
				case 5: sExposureProgram = CNLS::GetString(_T("Creative program")); break;
				case 6: sExposureProgram = CNLS::GetString(_T("Action program")); break;
				case 7: sExposureProgram = CNLS::GetString(_T("Portrait mode")); break;
				case 8: sExposureProgram = CNLS::GetString(_T("Landscape mode")); break;
				default: sExposureProgram.Format(_T("%d"), pEXIFReader->GetExposureProgram()); break;
				}
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exposure program:")), sExposureProgram);
			}
			if (pEXIFReader->GetExposureBiasPresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exposure bias (EV):")), pEXIFReader->GetExposureBias(), 2);
			}
			if (pEXIFReader->GetFlashFiredPresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Flash fired:")), pEXIFReader->GetFlashFired() ? CNLS::GetString(_T("yes")) : CNLS::GetString(_T("no")));
			}
			if (pEXIFReader->GetFocalLengthPresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Focal length (mm):")), pEXIFReader->GetFocalLength(), 1);
			}
			if (pEXIFReader->GetFNumberPresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("𝑓-Number:")), pEXIFReader->GetFNumber(), 1);
			}
			if (pEXIFReader->GetISOSpeedPresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("ISO Speed:")), (int)pEXIFReader->GetISOSpeed());
			}
			if (pEXIFReader->GetMeteringModePresent()) {
				CString sMeteringMode;
				switch (pEXIFReader->GetMeteringMode()) {
				case 1: sMeteringMode = CNLS::GetString(_T("Average")); break;
				case 2: sMeteringMode = CNLS::GetString(_T("Center weighted average")); break;
				case 3: sMeteringMode = CNLS::GetString(_T("Spot")); break;
				case 4: sMeteringMode = CNLS::GetString(_T("Multi-spot")); break;
				case 5: sMeteringMode = CNLS::GetString(_T("Pattern")); break;
				case 6: sMeteringMode = CNLS::GetString(_T("Partial")); break;
				case 255: sMeteringMode = CNLS::GetString(_T("Other")); break;
				default: sMeteringMode.Format(_T("%d"), pEXIFReader->GetMeteringMode()); break;
				}
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Metering mode:")), sMeteringMode);
			}
			if (pEXIFReader->GetWhiteBalancePresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("White balance:")), pEXIFReader->GetWhiteBalance() == 0 ? CNLS::GetString(_T("Auto")) : CNLS::GetString(_T("Manual")));
			}
			if (pEXIFReader->GetSceneCaptureTypePresent()) {
				CString sSceneCapture;
				switch (pEXIFReader->GetSceneCaptureType()) {
				case 1: sSceneCapture = CNLS::GetString(_T("Landscape")); break;
				case 2: sSceneCapture = CNLS::GetString(_T("Portrait")); break;
				case 3: sSceneCapture = CNLS::GetString(_T("Night scene")); break;
				default: sSceneCapture = CNLS::GetString(_T("Standard")); break;
				}
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Scene type:")), sSceneCapture);
			}
			if (pEXIFReader->GetSoftwarePresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Software:")), pEXIFReader->GetSoftware());
			}
			if (pEXIFReader->GetXPCommentPresent()) {
				sComment = pEXIFReader->GetXPComment();
			}
		}
		else if (pRawMetaData != NULL) {
			if (pRawMetaData->GetAcquisitionTime().wYear > 1985) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Acquisition date:")), pRawMetaData->GetAcquisitionTime());
			}
			else {
				const FILETIME* pFileTime = pFileList->CurrentModificationTime();
				if (pFileTime != NULL) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modification date:")), *pFileTime);
				}
			}
			if (pRawMetaData->IsGPSInformationPresent()) {
				CString sGPSLocation = CreateGPSString(pRawMetaData->GetGPSLatitude(), pRawMetaData->GetGPSLongitude());
				m_pEXIFDisplay->SetGPSLocation(sGPSLocation, CreateGPSURL(pRawMetaData->GetGPSLatitude(), pRawMetaData->GetGPSLongitude()));
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Location:")), sGPSLocation, true);
				if (pRawMetaData->IsGPSAltitudePresent()) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Altitude (m):")), pRawMetaData->GetGPSAltitude(), 0);
				}
			}
			if (pRawMetaData->GetManufacturer()[0] != 0) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Camera model:")), CString(pRawMetaData->GetManufacturer()) + _T(" ") + pRawMetaData->GetModel());
			}
			if (pRawMetaData->GetExposureTime() > 0.0) {
				double exposureTime = pRawMetaData->GetExposureTime();
				Rational rational = (exposureTime < 1.0) ? Rational(1, Helpers::RoundToInt(1.0 / exposureTime)) : Rational(Helpers::RoundToInt(exposureTime), 1);
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exposure time (s):")), rational);
			}
			if (pRawMetaData->IsFlashFired()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Flash fired:")), CNLS::GetString(_T("yes")));
			}
			if (pRawMetaData->GetFocalLength() > 0.0) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Focal length (mm):")), pRawMetaData->GetFocalLength(), 1);
			}
			if (pRawMetaData->GetAperture() > 0.0) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("F-Number:")), pRawMetaData->GetAperture(), 1);
			}
			if (pRawMetaData->GetIsoSpeed() > 0.0) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("ISO Speed:")), (int)pRawMetaData->GetIsoSpeed());
			}
		}
		else {
			const FILETIME* pFileTime = pFileList->CurrentModificationTime();
			if (pFileTime != NULL) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modification date:")), *pFileTime);
			}
		}
	}

	if (sComment == NULL || sComment[0] == 0 || ((std::wstring)sComment).find_first_not_of(L" \t\n\r\f\v", 0) == std::wstring::npos) {
		sComment = CurrentImage()->GetJPEGComment();
	}
	if (CSettingsProvider::This().ShowJPEGComments() && sComment != NULL && sComment[0] != 0) {
		m_pEXIFDisplay->SetComment(sComment);
	}
}

bool CEXIFDisplayCtl::OnMouseMove(int nX, int nY) {
	bool bHandled = CPanelController::OnMouseMove(nX, nY);
	bool bMouseOver = m_pEXIFDisplay->PanelRect().PtInRect(CPoint(nX, nY));
	m_pEXIFDisplay->GetControl<CButtonCtrl*>(CEXIFDisplay::ID_btnClose)->SetShow(bMouseOver);
	return bHandled;
}

void CEXIFDisplayCtl::OnShowHistogram(void* pContext, int nParameter, CButtonCtrl & sender) {
	CEXIFDisplayCtl* pThis = (CEXIFDisplayCtl*)pContext;
	pThis->m_pEXIFDisplay->SetShowHistogram(!pThis->m_pEXIFDisplay->GetShowHistogram());
	pThis->m_pEXIFDisplay->RequestRepositioning();
	pThis->InvalidateMainDlg();
}

void CEXIFDisplayCtl::OnClose(void* pContext, int nParameter, CButtonCtrl & sender) {
	CEXIFDisplayCtl* pThis = (CEXIFDisplayCtl*)pContext;
	pThis->m_pMainDlg->ExecuteCommand(IDM_SHOW_FILEINFO);
}
