#include <QApplication>
#include <QMetaObject>

#include <jni.h>

#include "android_runtime_bridge.h"
#include "android_window.h"
#include "desktop/src/qt/settings/app_settings.h"

namespace
{
AndroidWindow* g_window = nullptr;

void QueueAppClosing()
{
    QMetaObject::invokeMethod(qApp, [] {
        if (g_window)
            g_window->appClosing();
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
Java_org_atemvegeta_pocketwalker_PocketWalkerService_nativeOnAppClosing(JNIEnv*, jclass)
{
    QueueAppClosing();
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
    return result;
}
