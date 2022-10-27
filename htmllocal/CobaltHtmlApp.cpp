/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CobaltHtmlApp.h"

namespace WPEFramework {

namespace CobaltHtmlApp {

extern Exchange::IMemory* MemoryObserver(const RPC::IRemoteConnection* connection);

}  // namespace CobaltHtmlApp

namespace Plugin {

SERVICE_REGISTRATION(CobaltHtmlApp, 1, 0);

static Core::ProxyPoolType<Web::TextBody> _textBodies(2);
static Core::ProxyPoolType<Web::JSONBodyType<CobaltHtmlApp::Data>> jsonBodyDataFactory(2);

/* encapsulated class Thread  */
const string CobaltHtmlApp::Initialize(PluginHost::IShell *service) {
  Config config;
  string message;

  ASSERT(_service == nullptr);
  ASSERT(_cobalt == nullptr);
  ASSERT(_memory == nullptr);

  config.FromString(service->ConfigLine());
  syslog(LOG_ERR, "Ness CobaltHtmlApp::Initialize ConfigLine = %s\n",
      service->ConfigLine().c_str());

  _connectionId = 0;
  _service = service;
  _skipURL = _service->WebPrefix().length();

  // Register the Connection::Notification stuff. The Remote process might die
  // before we get a
  // change to "register" the sink for these events !!! So do it ahead of
  // instantiation.
  _service->Register(&_notification);
  _cobalt = _service->Root < Exchange::IBrowser > (_connectionId, 2000, _T("CobaltHtmlAppImplementation"));

  if (_cobalt != nullptr) {
    PluginHost::IStateControl *stateControl(
      _cobalt->QueryInterface<PluginHost::IStateControl>());
    if (stateControl == nullptr) {
      _cobalt->Release();
      _cobalt = nullptr;
    } else {
      RPC::IRemoteConnection* remoteConnection = _service->RemoteConnection(_connectionId);
      _memory = WPEFramework::CobaltHtmlApp::MemoryObserver(remoteConnection);
      ASSERT(_memory != nullptr);
      if (remoteConnection)
        remoteConnection->Release();

      _cobalt->Register(&_notification);
      stateControl->Register(&_notification);
      stateControl->Configure(_service);
      stateControl->Release();
    }
  }

  if (_cobalt == nullptr) {
    message = _T("CobaltHtmlApp could not be instantiated.");
    _service->Unregister(&_notification);
    ConnectionTermination(_connectionId);
    _service = nullptr;
  }

  return message;
}

void CobaltHtmlApp::Deinitialize(PluginHost::IShell *service) {
  ASSERT(_service == service);
  ASSERT(_cobalt != nullptr);
  ASSERT(_memory != nullptr);

  if (_cobalt == nullptr)
      return;

  PluginHost::IStateControl *stateControl(
    _cobalt->QueryInterface<PluginHost::IStateControl>());

  // Make sure the Activated and Deactivated are no longer called before we
  // start cleaning up..
  _service->Unregister(&_notification);
  _cobalt->Unregister(&_notification);
  _memory->Release();

  // In case CobaltHtmlApp crashed, there is no access to the statecontrol interface,
  // check it !!
  if (stateControl != nullptr) {
    stateControl->Unregister(&_notification);
    stateControl->Release();
  } else {
    // On behalf of the crashed process, we will release the notification sink.
    _notification.Release();
  }

  if (_cobalt->Release() != Core::ERROR_DESTRUCTION_SUCCEEDED) {
    ASSERT(_connectionId != 0);
    TRACE_L1("CobaltHtmlApp Plugin is not properly destructed. %d", _connectionId);
    ConnectionTermination(_connectionId);
  }

  // Deinitialize what we initialized..
  _memory = nullptr;
  _cobalt = nullptr;
  _service = nullptr;
}

string CobaltHtmlApp::Information() const {
  // No additional info to report.
  return (string());
}

void CobaltHtmlApp::Inbound(Web::Request &request) {
  if (request.Verb == Web::Request::HTTP_POST) {
    // This might be a "launch" application thingy, make sure we receive the
    // proper info.
    request.Body(jsonBodyDataFactory.Element());
  }
}

Core::ProxyType<Web::Response> CobaltHtmlApp::Process(const Web::Request &request) {
  ASSERT(_skipURL <= request.Path.length());
  TRACE(Trace::Information, (string(_T("Received cobalt request"))));

  Core::ProxyType<Web::Response> result(PluginHost::IFactories::Instance().Response());

  Core::TextSegmentIterator index(
    Core::TextFragment(request.Path, _skipURL,
                       request.Path.length() - _skipURL), false, '/');

  result->ErrorCode = Web::STATUS_BAD_REQUEST;
  result->Message = "Unknown error";

  if (request.Verb == Web::Request::HTTP_POST) {
    // We might be receiving a plugin download request.
    if ((index.Next() == true) && (index.Next() == true)
        && (_cobalt != nullptr)) {
      PluginHost::IStateControl *stateControl(
        _cobalt->QueryInterface<PluginHost::IStateControl>());
      if (stateControl != nullptr) {
        if (index.Remainder() == _T("Suspend")) {
          stateControl->Request(PluginHost::IStateControl::SUSPEND);
        } else if (index.Remainder() == _T("Resume")) {
          stateControl->Request(PluginHost::IStateControl::RESUME);
        } else if ((index.Remainder() == _T("URL"))
                   && (request.HasBody() == true)
                   && (request.Body<const Data>()->URL.Value().empty()
                       == false)) {
          _cobalt->SetURL(request.Body<const Data>()->URL.Value());
        }
        stateControl->Release();
      }
    }
  }
  else if (request.Verb == Web::Request::HTTP_GET) {
  }
  return result;
}

void CobaltHtmlApp::LoadFinished(const string &URL) {
  string message(
    string("{ \"url\": \"") + URL + string("\", \"loaded\":true }"));
  TRACE(Trace::Information, (_T("LoadFinished: %s"), message.c_str()));
  _service->Notify(message);

  event_urlchange(URL, true);
}

void CobaltHtmlApp::URLChanged(const string &URL) {
  string message(string("{ \"url\": \"") + URL + string("\" }"));
  TRACE(Trace::Information, (_T("URLChanged: %s"), message.c_str()));
  _service->Notify(message);

  event_urlchange(URL, false);
}

void CobaltHtmlApp::Hidden(const bool hidden) {
  TRACE(Trace::Information,
        (_T("Hidden: %s }"), (hidden ? "true" : "false")));
  string message(
    string("{ \"hidden\": ") + (hidden ? _T("true") : _T("false"))
    + string("}"));
  _hidden = hidden;
  _service->Notify(message);

  event_visibilitychange(hidden);
}

void CobaltHtmlApp::StateChange(const PluginHost::IStateControl::state state) {
  switch (state) {
    case PluginHost::IStateControl::RESUMED:
      TRACE(Trace::Information,
            (string(_T("StateChange: { \"suspend\":false }"))));
      _service->Notify("{ \"suspended\":false }");
      event_statechange(false);
      break;
    case PluginHost::IStateControl::SUSPENDED:
      TRACE(Trace::Information,
            (string(_T("StateChange: { \"suspend\":true }"))));
      _service->Notify("{ \"suspended\":true }");
      event_statechange(true);
      break;
    case PluginHost::IStateControl::EXITED:
      // Exited by CobaltHtmlApp app
      Core::IWorkerPool::Instance().Submit(
        PluginHost::IShell::Job::Create(_service,
                                        PluginHost::IShell::DEACTIVATED,
                                        PluginHost::IShell::REQUESTED));
      break;
    case PluginHost::IStateControl::UNINITIALIZED:
      break;
    default:
      ASSERT(false);
      break;
  }
}

void CobaltHtmlApp::Deactivated(RPC::IRemoteConnection *connection) {
  if (connection->Id() == _connectionId) {
    ASSERT(_service != nullptr);
    Core::IWorkerPool::Instance().Submit(
      PluginHost::IShell::Job::Create(_service,
        PluginHost::IShell::DEACTIVATED,
        PluginHost::IShell::FAILURE));
  }
}

}  // namespace Plugin
}  // namespace WPEFramework
