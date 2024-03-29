// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/usb/web_usb_service_impl.h"

#include <utility>

#include "base/bind.h"
#include "device/base/device_client.h"
#include "device/usb/mojo/device_manager_impl.h"
#include "device/usb/mojo/type_converters.h"
#include "device/usb/usb_device.h"

// static
void WebUsbServiceImpl::Create(
    base::WeakPtr<device::usb::PermissionProvider> permission_provider,
    base::WeakPtr<WebUsbChooser> usb_chooser,
    blink::mojom::WebUsbServiceRequest request) {
  // Bind the Blink request with WebUsbServiceImpl.
  auto* web_usb_service = new WebUsbServiceImpl(std::move(permission_provider),
                                                std::move(usb_chooser));
  web_usb_service->binding_ = mojo::MakeStrongBinding(
      base::WrapUnique(web_usb_service), std::move(request));
}

WebUsbServiceImpl::WebUsbServiceImpl(
    base::WeakPtr<device::usb::PermissionProvider> permission_provider,
    base::WeakPtr<WebUsbChooser> usb_chooser)
    : permission_provider_(std::move(permission_provider)),
      usb_chooser_(std::move(usb_chooser)),
      observer_(this),
      weak_factory_(this) {
  // Bind |device_manager_| to UsbDeviceManager and set error handler.
  // TODO(donna.wu@intel.com): Request UsbDeviceManagerPtr from the Device
  // Service after moving //device/usb to //services/device.
  device::usb::DeviceManagerImpl::Create(mojo::MakeRequest(&device_manager_));
  device_manager_.set_connection_error_handler(base::BindOnce(
      &WebUsbServiceImpl::OnConnectionError, base::Unretained(this)));
  // Listen for add/remove device events from UsbService.
  // TODO(donna.wu@intel.com): Listen to |device_manager_| in the future.
  // We can't set WebUsbServiceImpl as a UsbDeviceManagerClient because
  // the OnDeviceRemoved event will be delivered here after it is delivered
  // to UsbChooserContext, meaning that all ephemeral permission checks in
  // OnDeviceRemoved() will fail.
  auto* usb_service = device::DeviceClient::Get()->GetUsbService();
  if (usb_service)
    observer_.Add(usb_service);
}

WebUsbServiceImpl::~WebUsbServiceImpl() = default;

void WebUsbServiceImpl::GetDevices(GetDevicesCallback callback) {
  device_manager_->GetDevices(
      nullptr, base::BindOnce(&WebUsbServiceImpl::OnGetDevices,
                              weak_factory_.GetWeakPtr(), std::move(callback)));
}

void WebUsbServiceImpl::OnGetDevices(
    GetDevicesCallback callback,
    std::vector<device::mojom::UsbDeviceInfoPtr> device_info_list) {
  std::vector<device::mojom::UsbDeviceInfoPtr> device_infos;
  for (auto& device_info : device_info_list) {
    if (device_info && permission_provider_ &&
        permission_provider_->HasDevicePermission(*device_info)) {
      device_infos.push_back(device_info.Clone());
    }
  }
  std::move(callback).Run(std::move(device_infos));
}

void WebUsbServiceImpl::GetDevice(
    const std::string& guid,
    device::mojom::UsbDeviceRequest device_request) {
  // Try to bind with the new device to be created for DeviceOpened/Closed
  // events. It is safe to pass this request directly to |device_manager_|
  // because |guid| is unguessable.
  device::mojom::UsbDeviceClientPtr device_client;
  device_client_bindings_.AddBinding(this, mojo::MakeRequest(&device_client));
  device_manager_->GetDevice(guid, std::move(device_request),
                             std::move(device_client));
}

void WebUsbServiceImpl::GetPermission(
    std::vector<device::mojom::UsbDeviceFilterPtr> device_filters,
    GetPermissionCallback callback) {
  if (!usb_chooser_)
    std::move(callback).Run(nullptr);

  usb_chooser_->GetPermission(std::move(device_filters), std::move(callback));
}

void WebUsbServiceImpl::SetClient(
    device::mojom::UsbDeviceManagerClientPtr client) {
  client_ = std::move(client);
}

void WebUsbServiceImpl::OnDeviceAdded(scoped_refptr<device::UsbDevice> device) {
  auto device_info = device::mojom::UsbDeviceInfo::From(*device);
  DCHECK(device_info);
  if (client_ && permission_provider_ &&
      permission_provider_->HasDevicePermission(*device_info)) {
    client_->OnDeviceAdded(std::move(device_info));
  }
}

void WebUsbServiceImpl::OnDeviceRemoved(
    scoped_refptr<device::UsbDevice> device) {
  auto device_info = device::mojom::UsbDeviceInfo::From(*device);
  DCHECK(device_info);
  if (client_ && permission_provider_ &&
      permission_provider_->HasDevicePermission(*device_info)) {
    client_->OnDeviceRemoved(std::move(device_info));
  }
}

// device::mojom::UsbDeviceClient implementation:
void WebUsbServiceImpl::OnDeviceOpened() {
  if (permission_provider_)
    permission_provider_->IncrementConnectionCount();
}

void WebUsbServiceImpl::OnDeviceClosed() {
  if (permission_provider_)
    permission_provider_->DecrementConnectionCount();
}

void WebUsbServiceImpl::OnConnectionError() {
  device_manager_.reset();

  // Close the connection with blink.
  client_.reset();
  binding_->Close();
}

void WebUsbServiceImpl::WillDestroyUsbService() {
  OnConnectionError();
}
