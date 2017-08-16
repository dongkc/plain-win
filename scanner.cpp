// scanner.cpp : 定义控制台应用程序的入口点。
//

#include <tchar.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "twpp.hpp"
#include "glog/logging.h"

using namespace std;
using namespace Twpp;

Manager mgr(
    Identity(
      Version(1, 0, Language::English, Country::UnitedKingdom, "v1.0"),
      DataGroup::Image,
      "Broke & Company",
      "BC Scan",
      "BC Soft Scan"
      )
    );

int scan_doc(std::string& image)
{
  try {

    ReturnCode rc;
    bool ret = mgr.load();

    HWND raw_handle = GetActiveWindow();
    Handle handle(raw_handle);

    if(mgr.open(handle) != ReturnCode::Success) {
      LOG(INFO) << "mgr.open failed";
    }

    //vector<Source> sources;
    //mgr.sources(sources);
    //Source userSrc;
    //mgr.showSourceDialog(userSrc);
    Source src;
    if(mgr.defaultSource(src) != ReturnCode::Success) {
      LOG(INFO) << "mgr.defaultSource failed";
    }

    if(src.open() != ReturnCode::Success) {
      LOG(INFO) << "src.open failed";
    };
    Capability supported(CapType::SupportedCaps);
    src.capability(Msg::Get, supported);

    for (CapType cap : supported.data<CapType::SupportedCaps>()){
      // iterate over supported capabilities
    }

    // set image transfer mechanism
    Capability xferMech = Capability::createOneValue<CapType::IXferMech>(XferMech::Native);
    src.capability(Msg::Set, xferMech);

    if (src.enable(UserInterface(true, false, handle)) != ReturnCode::Success) {
      LOG(INFO) << "src.enable failed";
    };
    if(src.waitReady() != ReturnCode::Success) {
      LOG(INFO) << "src.waitReady failed";
    }

    ImageNativeXfer xfer;
    rc = src.imageNativeXfer(xfer);

    auto lock = xfer.data(); // also xfer.data<YourDataType>()
    // lock.data() returns pointer to image data
    // you can also use `lock->member` to access member variables

    // e.g. on Windows:
    auto pBIH = xfer.data<BITMAPINFOHEADER>();

    BITMAPFILEHEADER bfh;
    bfh.bfType = ( (WORD) ('M' << 8) | 'B');
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
                 ((((pBIH->biWidth * pBIH->biBitCount + 31)/32) * 4) * pBIH->biHeight) +
                 pBIH->biClrUsed * sizeof(RGBQUAD);
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
                    pBIH->biClrUsed * sizeof(RGBQUAD);

    image = string((const char*)&bfh, sizeof(BITMAPFILEHEADER)) + string((const char*)pBIH.data(), sizeof(BITMAPINFOHEADER) + pBIH->biClrUsed * sizeof(RGBQUAD) + pBIH->biSizeImage);
#if 0
  {
    ofstream of(path, ios::binary);
    BITMAPFILEHEADER bfh;
    bfh.bfType = ( (WORD) ('M' << 8) | 'B');
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
                 ((((pBIH->biWidth * pBIH->biBitCount + 31)/32) * 4) * pBIH->biHeight) +
                 pBIH->biClrUsed * sizeof(RGBQUAD);
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
                    pBIH->biClrUsed * sizeof(RGBQUAD);

    //写文件头进文件
    of.write((const char*)&bfh, sizeof(BITMAPFILEHEADER));
    of.write((const char*)pBIH.data(), bfh.bfSize - sizeof(BITMAPFILEHEADER));
  }
#endif

  src.disable();
  src.close();
  src.cleanup();
  }catch (const std::exception& e) {
    LOG(INFO) << "scan exception: " << e.what();
  }

  return 0;
}
