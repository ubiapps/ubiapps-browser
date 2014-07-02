// Copyright (c) 2012 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

// This file is shared by webinosRenderer and cef_unittests so don't include using
// a qualified path.
#include "client_app.h"  // NOLINT(build/include)

#include <string>

#include "include/cef_cookie.h"
#include "include/cef_process_message.h"
#include "include/cef_task.h"
#include "include/cef_v8.h"
#include "base/files/file_path.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/values.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "util.h"  // NOLINT(build/include)
#include "webinosBrowser/WebinosConstants.h"
#include "webinosBrowser/webinosBrowser.h"
#include "webinosBrowser/client_switches.h"
#include "webinosBrowser/WidgetConfig.h"

ClientApp::ClientApp() : 
  m_webinosShowChrome(false)
{
  // Default schemes that support cookies.
  cookieable_schemes_.push_back("http");
  cookieable_schemes_.push_back("https");
}

void ClientApp::OnContextInitialized() 
{
  // Register cookieable schemes with the global cookie manager.
  CefRefPtr<CefCookieManager> manager = CefCookieManager::GetGlobalManager();
  ASSERT(manager.get());
  manager->SetSupportedSchemes(cookieable_schemes_);
}

// Inject webinos.js
// The file is loaded from the webinos\test\client folder if possible.
// If this fails, the current folder is used.
void ClientApp::InjectWebinos(CefRefPtr<CefFrame> frame)
{
  CefRefPtr<CefCommandLine> commandLine = AppGetCommandLine();

  // First try and load the platform-supplied webinos.js
  std::string pzpPath = AppGetWebinosWRTConfig(NULL,NULL);
  CefString wrtPath;

  // Make sure there is a trailing separator on the path.
  if (pzpPath.length() > 0) 
  {
    if (pzpPath.find_last_of('/') == pzpPath.length()-1 || pzpPath.find_last_of('\\') == pzpPath.length()-1)
      wrtPath = pzpPath + "wrt/webinos.js";
    else
      wrtPath = pzpPath + "/wrt/webinos.js";
  }

#if defined(OS_WIN)
  base::FilePath webinosJSPath(wrtPath.ToWString().c_str());
#else
  base::FilePath webinosJSPath(wrtPath);
#endif

  LOG(INFO) << "webinos.js path is " << wrtPath;

  int64 webinosJSCodeSize;
  bool gotJSFile = base::GetFileSize(webinosJSPath, &webinosJSCodeSize);
  if (gotJSFile)
  {
    char* webinosJSCode = new char[webinosJSCodeSize+1];
    base::ReadFile(webinosJSPath, webinosJSCode, webinosJSCodeSize);
    webinosJSCode[webinosJSCodeSize] = 0;

    if (frame == NULL)
    {
      // Register as a Cef extension.
      CefRegisterExtension("webinos", webinosJSCode, NULL);
    }
    else
    {
      // Run the code in the frame javascript context right now,
      // but only if the URL refers to the widget server.
      int widgetServerPort;
      AppGetWebinosWRTConfig(NULL,&widgetServerPort);

      char injectionCandidate[MAX_URL_LENGTH];
      sprintf(injectionCandidate,"http://localhost:%d",widgetServerPort);

      std::string url = frame->GetURL();
      if (url.substr(0,strlen(injectionCandidate)) == injectionCandidate)
        frame->ExecuteJavaScript(webinosJSCode, url, 0);
    }

    delete[] webinosJSCode;
  }
  else
  {
    	LOG(ERROR) << "Can't find webinos.js";
  }
}

// Inject webinos.js as a Cef Extension.
// Deprecated - now injected when the context is initialised, 
// this change was made for timing reasons.
void ClientApp::OnWebKitInitialized() 
{
  //InjectWebinos(NULL);
}

void ClientApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) 
{
	/**
      N.B. this code is poor and needs tidying up
    **/

    std::string bootData("(function () { "\
"	try { "\
"		// Widget interface is injected here."\
"		%s"\
"	} catch (e) { "\
"		alert(\"webinos boot code exception: \" + e); "\
"	} "\
"}());");

      std::string widgetInterface;

      webinos::WidgetConfig cfg;
      if (cfg.LoadFromURL(frame->GetURL()))
      {
        std::string widgetArgs;
        AppGetWidgetArgs(cfg.GetSessionId(),widgetArgs);

        /*
          We need to expose the w3c widget interface (http://www.w3.org/TR/widgets-apis/)

          interface Widget {
              readonly attribute DOMString     author;
              readonly attribute DOMString     description;
              readonly attribute DOMString     name;
              readonly attribute DOMString     shortName;
              readonly attribute DOMString     version;
              readonly attribute DOMString     id;
              readonly attribute DOMString     authorEmail;
              readonly attribute DOMString     authorHref;
              readonly attribute WidgetStorage preferences;     <!-- TBD!
              readonly attribute unsigned long height;
              readonly attribute unsigned long width;
          };
        */
        std::stringstream wifStr(std::stringstream::in | std::stringstream::out);

        wifStr << "\
                           // Widget interface\r\n\
                           window.widget = { \r\n\
                           author: \"" << cfg.author() << "\",\r\n\
                           description: \"" << cfg.description() << "\",\r\n\
                           name: \"" << cfg.name() << "\",\r\n\
                           shortName: \"" << cfg.shortName() << "\",\r\n\
                           version: \"" << cfg.version() << "\",\r\n\
                           id: \"" << cfg.id() << "\",\r\n\
                           authorEmail: \"" << cfg.authorEmail() << "\",\r\n\
                           authorHref: \"" << cfg.authorHref() << "\",\r\n\
                           preferences: {},\r\n\
                           height: " << cfg.height() << ",\r\n\
                           width: " << cfg.width() << ",\r\n\
                           args: {" << widgetArgs << "}\r\n\
                          }; ";

        widgetInterface = wifStr.str();
        LOG(INFO) << "widget interface is \r\n" << widgetInterface;
      }
      else
      {
    	  LOG(INFO) << "OnContextCreated => not a widget " << frame->GetURL();
      }

    int bootstrapLen = bootData.length() + widgetInterface.length() + 100;
      char* bootstrap = new char[bootstrapLen];
    sprintf(bootstrap,bootData.c_str(),widgetInterface.c_str());
      std::string url = frame->GetURL();
      frame->ExecuteJavaScript(bootstrap, url, 0);
      delete[] bootstrap;

      InjectWebinos(frame);
}

void ClientApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) 
{
}

