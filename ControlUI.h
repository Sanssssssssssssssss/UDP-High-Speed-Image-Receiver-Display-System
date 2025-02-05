#ifndef CONTROL_UI_H
#define CONTROL_UI_H

#include <QWidget>
#include <QSlider>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QComboBox>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>

class ControlUI : public QWidget {
    Q_OBJECT
public:
    explicit ControlUI(QWidget *parent = nullptr);

signals:
    // Signal to request a snapshot
    void snapshotRequested(const QString &directory);

    // Signal to request video recording
    void recordingRequested(const QString &directory, const QString &format);

    // Signal for horizontal image flipping
    void flipHorizontalRequested(bool enabled);

    // Signal for vertical image flipping
    void flipVerticalRequested(bool enabled);

    // Signals for image property adjustments
    void brightnessChanged(int value);
    void gammaChanged(int value);
    void sharpnessChanged(int value);
    void denoiseChanged(int value);

public slots:
    void onRecordingStateChanged(bool isRecording);

public slots:
    // FPS update
    void onFPSChanged(int fps);

private slots:
    // Brightness adjustment
    void onBrightnessChanged(int value);

    // Gamma adjustment
    void onGammaChanged(int value);

    // Sharpness adjustment
    void onSharpnessChanged(int value);

    // Denoise adjustment
    void onDenoiseChanged(int value);

    // Take a snapshot
    void onTakeSnapshot();

    // Start/stop video recording
    void onRecordVideo();

    // Browse for save directory
    void onBrowseSaveDirectory();

    // Toggle horizontal flipping
    void onFlipHorizontalChanged(bool checked);

    // Toggle vertical flipping
    void onFlipVerticalChanged(bool checked);

private:
    // UI elements
    QLabel *fpsLabel;                  // Label to display FPS
    QSlider *brightnessSlider;         // Brightness slider
    QLabel *brightnessValueLabel;      // Label to display brightness value
    QSlider *gammaSlider;              // Gamma slider
    QLabel *gammaValueLabel;           // Label to display gamma value
    QSlider *sharpnessSlider;          // Sharpness slider
    QLabel *sharpnessValueLabel;       // Label to display sharpness value
    QSlider *denoiseSlider;            // Denoise slider
    QLabel *denoiseValueLabel;         // Label to display denoise value
    QCheckBox *horizontalFlip;         // Checkbox for horizontal flipping
    QCheckBox *verticalFlip;           // Checkbox for vertical flipping
    QPushButton *snapshotButton;       // Button to take a snapshot
    QPushButton *recordButton;         // Button to start/stop recording
    QPushButton *browseButton;         // Button to browse save directory
    QLabel *saveDirectoryLabel;        // Label to display save directory path
    QComboBox *formatComboBox;         // Dropdown for selecting video format

    // Internal state management
    QString saveDirectory;             // Save directory path
    bool isRecording;                  // Recording state flag
    QTimer *recordingTimer;            // Timer for recording duration
    QElapsedTimer recordingElapsedTimer; // Elapsed timer to track recording time
};

#endif // CONTROL_UI_H






