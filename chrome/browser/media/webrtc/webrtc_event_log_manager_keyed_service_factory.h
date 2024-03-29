// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_WEBRTC_WEBRTC_EVENT_LOG_MANAGER_KEYED_SERVICE_FACTORY_H_
#define CHROME_BROWSER_MEDIA_WEBRTC_WEBRTC_EVENT_LOG_MANAGER_KEYED_SERVICE_FACTORY_H_

#include "base/macros.h"
#include "base/memory/singleton.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class KeyedService;

namespace content {
class BrowserContext;
}  // namespace content

// Produces WebRtcEventLogManagerKeyedService-s for non-incognito profiles.
class WebRtcEventLogManagerKeyedServiceFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static WebRtcEventLogManagerKeyedServiceFactory* GetInstance();

 protected:
  bool ServiceIsCreatedWithBrowserContext() const override;

 private:
  friend struct base::DefaultSingletonTraits<
      WebRtcEventLogManagerKeyedServiceFactory>;

  WebRtcEventLogManagerKeyedServiceFactory();
  ~WebRtcEventLogManagerKeyedServiceFactory() override;

  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;

  DISALLOW_COPY_AND_ASSIGN(WebRtcEventLogManagerKeyedServiceFactory);
};

#endif  // CHROME_BROWSER_MEDIA_WEBRTC_WEBRTC_EVENT_LOG_MANAGER_KEYED_SERVICE_FACTORY_H_
