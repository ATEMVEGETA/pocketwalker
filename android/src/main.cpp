#include <QApplication>
#include <QJniObject>
#include <QMetaObject>

#include <atomic>
#include <jni.h>

#include "android_runtime_bridge.h"
#include "android_window.h"
#include "desktop/src/qt/settings/app_settings.h"

namespace
{
constexpr auto ANDROID_ACTIVITY = "org/atemvegeta/pocketwalker/PocketWalkerActivity";
AndroidWindow* g_window = nullptr;
std::atomic<bool> g_background_checkpoint_complete = true;

void QueueAppClosing()
{
    QMetaObject::invokeMethod(qApp, [] {
        if (g_window)
            g_window->appClosing();
    }, Qt::QueuedConnection);
}

void QueueBackgroundCheckpoint()
{
    g_background_checkpoint_complete.store(false, std::memory_order_release);
    QMetaObject::invokeMethod(qApp, [] {
        if (g_window)
            g_window->checkpointForBackground();
        g_background_checkpoint_complete.store(true, std::memory_order_release);
    }, Qt::QueuedConnection);
}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_atemvegeta_pocketwalker_PocketWalkerService_nativeSetMotionEnabled(
    JNIEnv*, jclass, const jboolean enabled)
{
    return AndroidRuntimeBridge::SetAccelerometerEnabled(enabled == JNI_TRUE)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_atemvegeta_pocketwalker_PocketWalkerService_nativeOnAcceleration(
    JNIEnv*, jclass, const jfloat x, const jfloat y, const jfloat z)
{
    return AndroidRuntimeBridge::PushAccelerometerSample(x, y, z)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_atemvegeta_pocketwalker_PocketWalkerService_nativeOnMotionPulse(
    JNIEnv*, jclass)
{
    return AndroidRuntimeBridge::PushMotionPulse() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_atemvegeta_pocketwalker_PocketWalkerActivity_nativeOnFileSelectionResult(
    JNIEnv*, jclass, jint file_type, jboolean changed)
{
    QMetaObject::invokeMethod(qApp, [file_type, changed] {
        if (g_window)
            g_window->fileSelectionFinished(file_type, changed == JNI_TRUE);
    }, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_org_atemvegeta_pocketwalker_PocketWalkerActivity_nativeOnAppClosing(JNIEnv*, jclass)
{
    QueueAppClosing();
}

extern "C" JNIEXPORT void JNICALL
Java_org_atemvegeta_pocketwalker_PocketWalkerActivity_nativeOnAppBackgrounded(JNIEnv*, jclass)
{
    QueueBackgroundCheckpoint();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_atemvegeta_pocketwalker_PocketWalkerService_nativeIsBackgroundCheckpointComplete(
    JNIEnv*, jclass)
{
    return g_background_checkpoint_complete.load(std::memory_order_acquire)
        ? JNI_TRUE : JNI_FALSE;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("PocketWalker");
    QApplication::setOrganizationName("ATEMVEGETA");
    QApplication::setQuitOnLastWindowClosed(false);
    AppSettings::instance.load();

    AndroidWindow window;
    g_window = &window;
    window.showMaximized();

    const int result = app.exec();
    g_window = nullptr;
    AppSettings::instance.save();
    QJniObject::callStaticMethod<void>(ANDROID_ACTIVITY, "finishAfterNativeClose", "()V");
    return result;
}
