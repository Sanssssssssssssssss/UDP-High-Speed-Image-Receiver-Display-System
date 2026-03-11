#ifndef CONTROL_UI_H
#define CONTROL_UI_H

#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QGridLayout>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class ControlUI : public QWidget {
    Q_OBJECT
public:
    explicit ControlUI(QWidget *parent = nullptr);

signals:
    void snapshotRequested(const QString &directory);
    void recordingRequested(const QString &directory, const QString &format);
    void flipHorizontalRequested(bool enabled);
    void flipVerticalRequested(bool enabled);
    void brightnessChanged(int value);
    void gammaChanged(int value);
    void sharpnessChanged(int value);
    void denoiseChanged(int value);
    void receiverSettingsRequested(const QString &address, quint16 port);
    void aiDetectionToggled(bool enabled);
    void demoModeRequested(bool enabled);

public slots:
    void onRecordingStateChanged(bool recording);
    void onFPSChanged(int fps);
    void onPerformanceStatsChanged(const QString &statsText);
    void onReceiverStatusChanged(const QString &statusText);
    void onReceiverSettingsChanged(const QString &address, quint16 port);
    void onAiStatusChanged(const QString &statusText);
    void onDemoStateChanged(bool enabled, const QString &statusText);

private slots:
    void onBrightnessChanged(int value);
    void onGammaChanged(int value);
    void onSharpnessChanged(int value);
    void onDenoiseChanged(int value);
    void onTakeSnapshot();
    void onRecordVideo();
    void onBrowseSaveDirectory();
    void onFlipHorizontalChanged(bool checked);
    void onFlipVerticalChanged(bool checked);
    void onApplyReceiverSettings();
    void onAiDetectionChanged(bool checked);
    void onDemoModeChanged(bool checked);

private:
    QWidget *createImagePage();
    QWidget *createCapturePage();
    QWidget *createNetworkPage();
    QWidget *createAiPage();
    QPushButton *createPageButton(const QString &text, int pageIndex);
    void addSliderControl(QVBoxLayout *parentLayout,
                          const QString &labelText,
                          QLabel **valueLabelOut,
                          QSlider **sliderOut,
                          int minValue,
                          int maxValue,
                          int defaultValue,
                          const char *slot);
    void setCurrentPage(int pageIndex);

    QLabel *fpsLabel;
    QLabel *performanceLabel;

    QGridLayout *pageButtonLayout;
    QList<QPushButton *> pageButtons;
    QStackedWidget *pageStack;

    QSlider *brightnessSlider;
    QLabel *brightnessValueLabel;
    QSlider *gammaSlider;
    QLabel *gammaValueLabel;
    QSlider *sharpnessSlider;
    QLabel *sharpnessValueLabel;
    QSlider *denoiseSlider;
    QLabel *denoiseValueLabel;
    QCheckBox *horizontalFlip;
    QCheckBox *verticalFlip;

    QPushButton *snapshotButton;
    QPushButton *recordButton;
    QPushButton *browseButton;
    QLabel *saveDirectoryLabel;
    QComboBox *formatComboBox;

    QLineEdit *addressEdit;
    QSpinBox *portSpinBox;
    QLabel *receiverStatusLabel;
    QPushButton *applyReceiverButton;
    QCheckBox *demoModeCheckBox;
    QLabel *demoStatusLabel;

    QCheckBox *aiEnableCheckBox;
    QLabel *aiStatusLabel;

    QString saveDirectory;
    bool isRecording;
    QTimer *recordingTimer;
    QElapsedTimer recordingElapsedTimer;
};

#endif // CONTROL_UI_H
