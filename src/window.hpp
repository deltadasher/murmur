#pragma once
#include "client.hpp"
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QToolButton>
#include <map>

class ResponsePlot : public QWidget {
public:
    using QWidget::QWidget;
    murmur::Parameters parameters;
protected:
    void paintEvent(QPaintEvent*) override;
};
class EchoPlot : public QWidget {
public:
    using QWidget::QWidget;
    murmur::Parameters parameters;
protected:
    void paintEvent(QPaintEvent*) override;
};
class LevelMeter : public QWidget {
public:
    explicit LevelMeter(const QString& label,QWidget* parent=nullptr);
    void setLevel(float level);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString title;
    float db=-60;
};
class Window : public QWidget {
    Q_OBJECT
public:
    explicit Window(Client&,bool demo=false);
    void usePreset(const QString&);
    bool loadFile(const QString&);
protected:
    void resizeEvent(QResizeEvent*) override;
private:
    Client& client;
    bool demonstration=false,connected=false,ready=false,running=false,requested=false,starting=false,syncing=false,narrow=false,systemMuteKnown=false,systemMuted=false;
    murmur::Parameters params;
    QString backgroundWarning;
    std::map<QString,QDoubleSpinBox*> values;
    std::map<QString,QSlider*> sliders;
    QComboBox *microphone=nullptr,*presets=nullptr;
    QPushButton* startButton=nullptr;
    QCheckBox *preview=nullptr,*bypass=nullptr,*mute=nullptr;
    QLabel *state=nullptr,*service=nullptr,*guard=nullptr,*footerNote=nullptr;
    LevelMeter *inputMeter=nullptr,*outputMeter=nullptr;
    ResponsePlot* plot=nullptr;
    EchoPlot* echoPlot=nullptr;
    QWidget *tone=nullptr,*texture=nullptr,*cleanup=nullptr,*space=nullptr,*divider=nullptr;
    QLabel *productLabel=nullptr,*presetHint=nullptr,*headphonesHint=nullptr,*outputLabel=nullptr;
    QGridLayout* routing=nullptr;
    QVBoxLayout* outer=nullptr;
    QHBoxLayout* toggles=nullptr;
    QHBoxLayout* faders=nullptr;
    QLabel *toneTitle=nullptr,*toneGuide=nullptr,*textureTitle=nullptr,*textureGuide=nullptr,*textureNote=nullptr;
    std::map<QString,QWidget*> faderWidgets;
    std::map<QString,QHBoxLayout*> faderRows;
    std::map<QString,QHBoxLayout*> parameterRows;
    QTabWidget* tabs=nullptr;
    QHBoxLayout* workspace=nullptr;
    QWidget *presetRail=nullptr,*compactPresets=nullptr,*frequencyPanel=nullptr,*systemWarning=nullptr;
    QScrollArea *toneScroll=nullptr,*textureScroll=nullptr,*cleanupScroll=nullptr,*spaceScroll=nullptr;
    std::map<QString,QGridLayout*> moduleGrids;
    std::map<QString,QStringList> moduleKeys;
    QToolButton* frequenciesButton=nullptr;
    QPushButton* unmuteSystem=nullptr;
    QLabel* presetState=nullptr;
    std::map<QString,QPushButton*> presetButtons;
    void syncPresetSelection();
    QWidget* parameterControl(const QString&,const QString&,const QString&,double,double,double,double,const QString&,bool vertical=false);
    void setParameter(const QString&,double);
    void applyParameters(const murmur::Parameters&,bool custom=true);
    void syncControls();
    void updateReadouts();
    void receiveState(const QJsonObject&);
    void refreshStart();
    void updateToneDensity();
    void saveFile();
    void showHelp();
};
