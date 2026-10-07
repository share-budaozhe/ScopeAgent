// 离屏渲染 + 像素分析：验证波形轨迹确实绘制
#include <QApplication>
#include <QPixmap>
#include <QImage>
#include <cstdio>
#include "waveformwidget.h"
#include "virtualscopedriver.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    VirtualScopeDriver driver;
    DeviceConfig c; c.waveform = "sine"; c.frequencyHz = 1000; c.amplitudeVpp = 3.0;
    c.secPerDiv = 0.0002; c.voltsPerDiv = 1.0; c.triggerLevelV = 0.5; c.noiseLevel = 0.03;
    driver.apply(c);
    WaveformWidget w;
    w.setDriver(&driver);
    w.resize(920, 520);
    w.show();
    app.processEvents();
    QImage img = w.grab().toImage();
    img.save("render_scope.png");

    int bg = 0, trace = 0, grid = 0, trigger = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            QRgb p = img.pixel(x, y);
            int r = qRed(p), g = qGreen(p), b = qBlue(p);
            if (g > 120 && b > 100 && r < 100) trace++;          // 青绿轨迹
            else if (r > 200 && g > 120 && b < 100) trigger++;   // 橙色触发线
            else if (r < 30 && g < 30 && b < 30) bg++;           // 背景
            else grid++;                                          // 网格/文字
        }
    }
    printf("pixels: bg=%d grid/text=%d trace=%d trigger=%d\n", bg, grid, trace, trigger);
    bool ok = trace > 5000 && bg > 100000 && trigger > 100;
    printf("%s\n", ok ? "RENDER_OK" : "RENDER_FAIL");
    return ok ? 0 : 1;
}
