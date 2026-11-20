// Qualcomm counterpart of mtk-sms-fwk-ready: tell qcril that the telephony UI is ready ("ATEL UI ready").
// Until qcril hears it, its power optimisation holds incoming SMS while the screen is off and drops them
// after ~20 s without acking. Stock firmware sends it from a system_ext app that GSIs don't ship.
//
// Newer qcril (SM8750 / Android 16 vendor, e.g. ASUS ROG Phone 9) registers the OEM hook as an
// AIDL service (vendor.qti.hardware.radio.qcrilhook.IQtiOemHook/oemhookN) instead of the old
// HIDL one (vendor.qti.hardware.radio.qcrilhook@1.0::IQtiOemHook). Try AIDL first, fall back
// to HIDL for older vendors. The raw request format is the same for both:
//   "QOEMHOOK" (8 bytes) | hook id 0x8001a (int32) | payload len 1 (int32) | 0x01 (UI ready)

#define LOG_TAG "qcom-atel-ready"

#include <string.h>
#include <unistd.h>
#include <string>

#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
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

static const uint32_t kOemHookRawRequest = 2; // setCallback is 1
static const int32_t kSetAtelUiStatus = 0x8001a;
static const char *kAidlDescriptor = "vendor.qti.hardware.radio.qcrilhook.IQtiOemHook";

// Build the raw OEM hook buffer: "QOEMHOOK" | id | len | payload(1)
static void buildRequest(uint8_t *buf) {
	int32_t id = kSetAtelUiStatus, len = 1;
	memcpy(buf, "QOEMHOOK", 8);
	memcpy(buf + 8, &id, 4);
	memcpy(buf + 12, &len, 4);
	buf[16] = 1;
}

// AIDL path: vendor.qti.hardware.radio.qcrilhook.IQtiOemHook/oemhookN
//
// We only ever send a oneway request, so the class needs no real callbacks; it exists so
// AIBinder_prepareTransaction can write the interface token for the remote binder.
static void *onCreate(void * /*args*/) { return nullptr; }
static void onDestroy(void * /*userData*/) {}
static binder_status_t onTransact(AIBinder * /*binder*/, transaction_code_t /*code*/,
				  const AParcel * /*in*/, AParcel * /*out*/) {
	return STATUS_UNKNOWN_TRANSACTION;
}

static bool sendAidl(const char *instance) {
	std::string name = std::string(kAidlDescriptor) + "/" + instance;
	ndk::SpAIBinder binder(AServiceManager_checkService(name.c_str()));
	if (binder.get() == nullptr) return false;

	static AIBinder_Class *clazz =
		AIBinder_Class_define(kAidlDescriptor, onCreate, onDestroy, onTransact);
	if (clazz == nullptr || !AIBinder_associateClass(binder.get(), clazz)) {
		ALOGW("%s: AIDL associateClass failed", instance);
		return false;
	}

	uint8_t req[17];
	buildRequest(req);

	AParcel *data = nullptr;
	binder_status_t status = AIBinder_prepareTransaction(binder.get(), &data);
	if (status != STATUS_OK || data == nullptr) {
		ALOGW("%s: AIDL prepareTransaction failed: %d", instance, status);
		return false;
	}

	AParcel_writeInt32(data, 1); // serial
	AParcel_writeByteArray(data, reinterpret_cast<const int8_t *>(req), sizeof(req));

	AParcel *reply = nullptr;
	status = AIBinder_transact(binder.get(), kOemHookRawRequest, &data, &reply, FLAG_ONEWAY);
	if (status != STATUS_OK) {
		ALOGW("%s: AIDL transact failed: %d", instance, status);
		if (data) AParcel_delete(data);
		if (reply) AParcel_delete(reply);
		return false;
	}
	// On success AIBinder_transact consumed both parcels
	return true;
}

// HIDL path: vendor.qti.hardware.radio.qcrilhook@1.0::IQtiOemHook/oemhookN
static bool sendHidl(const char *instance) {
	static const char *kOemHook = "vendor.qti.hardware.radio.qcrilhook@1.0::IQtiOemHook";
	auto ret = defaultServiceManager()->get(kOemHook, instance);
	if (!ret.isOk()) return false;
	sp<IBase> hook = ret;
	if (hook == nullptr) return false;

	hidl_vec<int8_t> req(8 + 4 + 4 + 1);
	buildRequest(reinterpret_cast<uint8_t *>(req.data()));

	Parcel data, reply;
	size_t parent, child;
	data.writeInterfaceToken(kOemHook);
	data.writeInt32(1); // serial
	data.writeBuffer(&req, sizeof(req), &parent);
	writeEmbeddedToParcel(req, &data, parent, 0, &child);
	auto status = toBinder<IBase>(hook)->transact(kOemHookRawRequest, data, &reply, IBinder::FLAG_ONEWAY);
	if (status != android::OK) ALOGW("%s: HIDL transact failed: %d", instance, status);
	return status == android::OK;
}

static bool send(const char *instance) {
	// Prefer AIDL (newer qcril), fall back to HIDL (older qcril)
	if (sendAidl(instance)) return true;
	return sendHidl(instance);
}

int main(int argc, char **argv) {
	const char *instance = argc > 1 ? argv[1] : "oemhook0";

	ABinderProcess_setThreadPoolMaxThreadCount(0);
	ABinderProcess_startThreadPool();

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