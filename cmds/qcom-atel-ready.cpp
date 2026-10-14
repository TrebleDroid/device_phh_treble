// Qualcomm counterpart of mtk-sms-fwk-ready: tell qcril that the telephony UI is ready ("ATEL UI ready").
// Until qcril hears it, its power optimisation holds incoming SMS while the screen is off and drops them
// after ~20 s without acking. Stock firmware sends it from a system_ext app that GSIs don't ship.
// The request goes to IQtiOemHook::oemHookRawRequest as a raw HIDL parcel, so no QTI interface is needed.

#define LOG_TAG "qcom-atel-ready"

#include <string.h>
#include <unistd.h>
#include <android/hidl/manager/1.0/IServiceManager.h>
#include <hidl/HidlBinderSupport.h>
#include <hidl/ServiceManagement.h>
#include <log/log.h>

using ::android::sp;
using ::android::hardware::defaultServiceManager;
using ::android::hardware::hidl_vec;
using ::android::hardware::IBinder;
using ::android::hardware::Parcel;
using ::android::hardware::toBinder;
using ::android::hardware::writeEmbeddedToParcel;
using ::android::hidl::base::V1_0::IBase;

static const char *kOemHook = "vendor.qti.hardware.radio.qcrilhook@1.0::IQtiOemHook";
static const uint32_t kOemHookRawRequest = 2; // setCallback is 1
static const int32_t kSetAtelUiStatus = 0x8001a;

static bool send(const char *instance) {
	auto ret = defaultServiceManager()->get(kOemHook, instance);
	if (!ret.isOk()) return false;
	sp<IBase> hook = ret;
	if (hook == nullptr) return false;

	// "QOEMHOOK", request id, payload length, payload (1 = ready), in native byte order
	hidl_vec<int8_t> req(8 + 4 + 4 + 1);
	int32_t id = kSetAtelUiStatus, len = 1;
	memcpy(&req[0], "QOEMHOOK", 8);
	memcpy(&req[8], &id, 4);
	memcpy(&req[12], &len, 4);
	req[16] = 1;

	Parcel data, reply;
	size_t parent, child;
	data.writeInterfaceToken(kOemHook);
	data.writeInt32(1); // serial
	data.writeBuffer(&req, sizeof(req), &parent);
	writeEmbeddedToParcel(req, &data, parent, 0, &child);
	auto status = toBinder<IBase>(hook)->transact(kOemHookRawRequest, data, &reply, IBinder::FLAG_ONEWAY);
	if (status != android::OK) ALOGW("%s: transact failed: %d", instance, status);
	return status == android::OK;
}

int main(int argc, char **argv) {
	const char *instance = argc > 1 ? argv[1] : "oemhook0";

	// qcril registers the hook a few seconds after it starts, and right after a restart
	// we may still get the dead one: retry for about a minute
	for (int delay : {0, 1, 2, 4, 8, 15, 30}) {
		sleep(delay);
		if (send(instance)) {
			// qcril may ignore a request that arrives right after the hook registers, and oneway
			// means no ack: send once more after it settled, it only sets a flag
			sleep(5);
			send(instance);
			ALOGI("%s: ATEL UI ready sent", instance);
			return 0;
		}
	}
	ALOGW("%s: not reached", instance);
	return 1;
}
