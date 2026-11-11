#include "window.hpp"
#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QIcon>
#include <QTabBar>
#include <QGridLayout>
#include <QJsonArray>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <optional>

namespace {
class ModuleTabBar : public QTabBar {
protected:
    void paintEvent(QPaintEvent* event) override {
        QTabBar::paintEvent(event);
        if(!hasFocus()||currentIndex()<0)return;
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor("#e6ad66"),1,Qt::DashLine));
        painter.drawRoundedRect(QRectF(tabRect(currentIndex())).adjusted(3.5,3.5,-3.5,-3.5),3,3);
    }
};
class ModuleTabs : public QTabWidget {
public:
    ModuleTabs(){setTabBar(new ModuleTabBar);}
};
class ConsoleStyle : public QProxyStyle {
public:
    ConsoleStyle():QProxyStyle(QStyleFactory::create("Fusion")) {}
    static void arrow(QPainter* painter,const QRect& rect,bool up,bool enabled) {
        painter->save();painter->setRenderHint(QPainter::Antialiasing);painter->setPen(Qt::NoPen);painter->setBrush(QColor(enabled ? "#c9cbbb":"#747b68"));
        const QPointF c=rect.center();QPolygonF triangle;
        if(up)triangle<<QPointF(c.x()-3.5,c.y()+2)<<QPointF(c.x()+3.5,c.y()+2)<<QPointF(c.x(),c.y()-2);
        else triangle<<QPointF(c.x()-3.5,c.y()-2)<<QPointF(c.x()+3.5,c.y()-2)<<QPointF(c.x(),c.y()+2);
        painter->drawPolygon(triangle);painter->restore();
    }
    void drawPrimitive(PrimitiveElement element,const QStyleOption* option,QPainter* painter,const QWidget* widget=nullptr) const override {
        if(element==PE_IndicatorArrowDown||element==PE_IndicatorArrowUp||element==PE_IndicatorSpinUp||element==PE_IndicatorSpinDown){arrow(painter,option->rect,element==PE_IndicatorArrowUp||element==PE_IndicatorSpinUp,option->state&State_Enabled);return;}
        if(element==PE_IndicatorCheckBox){
            const bool enabled=option->state&State_Enabled,checked=option->state&State_On;
            painter->save();painter->setRenderHint(QPainter::Antialiasing);
            const QRectF box=QRectF(option->rect).adjusted(.5,.5,-.5,-.5);
            painter->setBrush(QColor(checked?(enabled?"#e6ad66":"#65533b"):"#23261f"));
            painter->setPen(QColor(enabled?(option->state&State_MouseOver?"#e6ad66":"#858d77"):"#555b4b"));painter->drawRoundedRect(box,3,3);
            if(checked){QPainterPath check;check.moveTo(box.left()+4,box.center().y());check.lineTo(box.left()+7,box.bottom()-4);check.lineTo(box.right()-3,box.top()+4);painter->setPen(QPen(QColor(enabled?"#252017":"#b4b6aa"),2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter->drawPath(check);}
            painter->restore();return;
        }
        QProxyStyle::drawPrimitive(element,option,painter,widget);
    }
    int pixelMetric(PixelMetric metric,const QStyleOption* option=nullptr,const QWidget* widget=nullptr) const override {
        if(metric==PM_IndicatorWidth||metric==PM_IndicatorHeight)return 18;
        return QProxyStyle::pixelMetric(metric,option,widget);
    }
    QPixmap standardPixmap(StandardPixmap value,const QStyleOption* option=nullptr,const QWidget* widget=nullptr) const override {
        if(value==SP_ArrowUp||value==SP_ArrowDown){QPixmap pixmap(12,12);pixmap.fill(Qt::transparent);QPainter painter(&pixmap);arrow(&painter,pixmap.rect(),value==SP_ArrowUp,!option||(option->state&State_Enabled));return pixmap;}
        return QProxyStyle::standardPixmap(value,option,widget);
    }
};
// Keep QSlider's native input and geometry, but express signed controls from zero.
class VoiceSlider : public QSlider {
public:
    VoiceSlider(Qt::Orientation orientation,int original,std::optional<int> neutral):QSlider(orientation),originalValue(original),neutralValue(neutral) {setMouseTracking(true);}
protected:
    void paintEvent(QPaintEvent*) override {
        QStyleOptionSlider option;initStyleOption(&option);
        const QRect handle=style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderHandle,this);
        const bool horizontal=orientation()==Qt::Horizontal;
        const int length=horizontal?handle.width():handle.height();
        const int span=(horizontal?width():height())-length;
        const auto position=[&](int v){return length/2.+QStyle::sliderPositionFromValue(minimum(),maximum(),v,span,option.upsideDown);};
        const qreal start=position(neutralValue.value_or(minimum())),end=position(sliderPosition());
        const qreal center=horizontal?handle.center().y()+.5:handle.center().x()+.5;
        const auto point=[&](qreal pos){return horizontal?QPointF(pos,center):QPointF(center,pos);};
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(isEnabled()?"#4a5142":"#34382f"),4,Qt::SolidLine,Qt::RoundCap));p.drawLine(point(length/2.),point(length/2.+span));
        if(start!=end){p.setPen(QPen(QColor(isEnabled()?"#d5a363":"#65533b"),4,Qt::SolidLine,Qt::RoundCap));p.drawLine(point(start),point(end));}
        if(neutralValue){p.setPen(QPen(QColor(isEnabled()?"#a1a597":"#555b4b"),1));if(horizontal)p.drawLine(QPointF(start,center-6),QPointF(start,center+6));else p.drawLine(QPointF(center-6,start),QPointF(center+6,start));}
        p.setPen(QPen(QColor(isEnabled()?"#eeebe3":"#737864"),1));p.setBrush(QColor(!isEnabled()?"#737864":isSliderDown()||underMouse()?"#e6ad66":"#dcd9ce"));
        p.drawRoundedRect(QRectF(handle).adjusted(.5,.5,-.5,-.5),3,3);
        if(hasFocus()){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#e6ad66"),1,Qt::DashLine));p.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),3,3);}
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton&&isEnabled()){setValue(originalValue);event->accept();return;}
        QSlider::mouseDoubleClickEvent(event);
    }
private:
    int originalValue;
    std::optional<int> neutralValue;
};
QLabel* text(const QString& value,const char* role="body") {
    auto* label=new QLabel(value); label->setObjectName(role);label->setProperty("role",role); label->setTextFormat(Qt::PlainText); return label;
}
QFrame* rule(Qt::Orientation orientation=Qt::Horizontal) {
    auto* frame=new QFrame; frame->setObjectName("rule");
    if (orientation==Qt::Horizontal) frame->setFixedHeight(1); else frame->setFixedWidth(1);
    return frame;
}
class PresetButton : public QPushButton {
public:
    PresetButton(const QString& name,const QString& description):QPushButton(name),detail(description) {
        setCheckable(true);setFixedHeight(48);setAccessibleName(name+" preset");setAccessibleDescription(description);setToolTip(description);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        if(isChecked()||underMouse()||isDown())p.fillRect(rect(),QColor(isChecked()?"#302b22":"#24271f"));
        if(isChecked())p.fillRect(QRect(0,10,3,height()-20),QColor("#e6ad66"));
        QFont f=font();f.setPixelSize(14);f.setWeight(QFont::DemiBold);p.setFont(f);
        p.setPen(QColor(!isEnabled()?"#797d72":isChecked()?"#e6ad66":"#efece4"));p.drawText(QRect(16,3,width()-24,23),Qt::AlignLeft|Qt::AlignVCenter,text());
        f.setPixelSize(12);f.setWeight(QFont::Normal);p.setFont(f);p.setPen(QColor(isEnabled()?"#b4b6aa":"#797d72"));p.drawText(QRect(16,25,width()-24,20),Qt::AlignLeft|Qt::AlignVCenter,detail);
        if(hasFocus()){p.setPen(QPen(QColor("#e6ad66"),1,Qt::DashLine));p.setBrush(Qt::NoBrush);p.drawRect(rect().adjusted(2,2,-3,-3));}
    }
private:
    QString detail;
};
QIcon moduleIcon(int module) {
    QPixmap pixmap(20,20);pixmap.fill(Qt::transparent);QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#c5b99e"),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(module==0){for(int i=0;i<3;++i){const int x=4+i*6,y=6+i*3;p.drawLine(x,3,x,17);p.fillRect(QRectF(x-2,y-2,4,4),QColor("#c5b99e"));}}
    if(module==1){QPainterPath wave;wave.moveTo(2,10);wave.lineTo(5,10);wave.lineTo(7,4);wave.lineTo(10,16);wave.lineTo(13,6);wave.lineTo(15,10);wave.lineTo(18,10);p.drawPath(wave);}
    if(module==2){p.drawLine(2,15,7,15);p.drawLine(7,15,7,5);p.drawLine(7,5,13,5);p.drawLine(13,5,13,15);p.drawLine(13,15,18,15);}
    if(module==3){p.drawLine(3,4,3,16);p.drawLine(9,7,9,16);p.drawLine(15,10,15,16);p.drawLine(1,16,18,16);}
    return QIcon(pixmap);
}
class Mark : public QWidget {
public:
    Mark() { setFixedSize(34,34); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(QColor("#e6ad66"),3,Qt::SolidLine,Qt::RoundCap));
        const int heights[]={8,19,28,15,8};
        for (int i=0;i<5;++i) p.drawLine(QPointF(5+i*6,17-heights[i]/2.),QPointF(5+i*6,17+heights[i]/2.));
    }
};
}

LevelMeter::LevelMeter(const QString& label,QWidget* parent):QWidget(parent),title(label) {
    setMinimumHeight(48); setMinimumWidth(180); setAccessibleName(label+" audio level");
}
void LevelMeter::setLevel(float level) { db=std::isfinite(level) ? std::clamp(level,-60.f,0.f):-60.f; update(); }
void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); QFont font=this->font(); font.setPixelSize(12);font.setFeature("tnum",1); p.setFont(font);
    const QFontMetrics metrics(font);const int readingWidth=metrics.horizontalAdvance("−60.0 dB");
    p.setPen(QColor("#b4b6aa")); p.drawText(QRect(0,0,width()-readingWidth-8,19),Qt::AlignLeft,metrics.elidedText(title,Qt::ElideRight,width()-readingWidth-8));
    p.setPen(QColor("#efece4")); p.drawText(QRect(width()-readingWidth,0,readingWidth,19),Qt::AlignRight,db<=-60 ? QString::fromUtf8("−∞ dB"):QString::number(db,'f',1)+" dB");
    const int count=std::clamp(width()/10,18,60); const double gap=3,cell=(width()-(count-1)*gap)/count;
    for (int i=0;i<count;++i) {
        const double threshold=-60+(i+1)*60./count;
        QColor color=threshold> -6 ? QColor("#e6ad66"):QColor("#97b691");
        p.fillRect(QRectF(i*(cell+gap),23,cell,7),db>=threshold ? color:QColor("#34382f"));
    }
    p.setPen(QColor("#91978a"));
    for (int n=0;n<4;++n){const QString label=QString::number(-60+n*20);const int w=metrics.horizontalAdvance(label);const int x=static_cast<int>(n*(width()-w)/3.);p.drawText(QRect(x,32,w,16),Qt::AlignLeft,label);}
}
void ResponsePlot::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area=rect().adjusted(38,12,-20,-25);
    if (area.width()<1 || area.height()<1) return;
    const auto x=[&](double hz) {return area.left()+std::log(hz/20)/std::log(1000.)*area.width();};
    const auto y=[&](double db) {return area.center().y()-db*area.height()/48.;};
    QFont font=this->font(); font.setPixelSize(11); painter.setFont(font);
    for (double db:{-18.,-9.,0.,9.,18.}) {
        painter.setPen(QPen(QColor(db==0 ? "#4b5044":"#30342c"),1,db==0 ? Qt::SolidLine:Qt::DotLine));
        painter.drawLine(QPointF(area.left(),y(db)),QPointF(area.right(),y(db)));
        if (db==0 || (area.height()>=44 && (db==-18 || db==18))) {painter.setPen(QColor("#b4b6aa")); painter.drawText(QRectF(0,y(db)-7,30,15),Qt::AlignRight,(db>0 ? "+":"")+QString::number(db));}
    }
    for (double hz:{50.,100.,500.,1000.,5000.,10000.}) {
        painter.setPen(QColor("#30342c")); painter.drawLine(QPointF(x(hz),area.top()),QPointF(x(hz),area.bottom()));
        painter.setPen(QColor("#b4b6aa")); painter.drawText(QRectF(x(hz)-20,area.bottom()+7,40,15),Qt::AlignCenter,hz>=1000 ? QString::number(hz/1000)+"k":QString::number(hz));
    }
    murmur::Biquad bass,mid,treble;
    bass.configure(murmur::Biquad::LowShelf,parameters.bassHz,parameters.bass,true);
    mid.configure(murmur::Biquad::Peak,parameters.midHz,parameters.mid,true);
    treble.configure(murmur::Biquad::HighShelf,parameters.trebleHz,parameters.treble,true);
    const auto response=[&](double hz){return parameters.bypass?0.:std::clamp(20*std::log10(bass.magnitude(hz)*mid.magnitude(hz)*treble.magnitude(hz)),-24.,24.);};
    QPainterPath path;
    for (int i=0;i<=350;++i) {double hz=20*std::pow(1000.,i/350.); QPointF point(x(hz),y(response(hz))); if (i==0) path.moveTo(point); else path.lineTo(point);}
    painter.save(); painter.setClipRect(area);
    auto fill=path; fill.lineTo(area.right(),y(0)); fill.lineTo(area.left(),y(0)); fill.closeSubpath(); painter.fillPath(fill,QColor(230,173,102,15));
    painter.setPen(QPen(QColor(parameters.bypass||!isEnabled() ? "#838676":"#e6ad66"),2)); painter.drawPath(path);
    painter.restore();painter.setPen(QColor("#b4b6aa"));painter.drawText(QRectF(0,0,30,12),Qt::AlignRight,"dB");painter.drawText(QRectF(width()-20,height()-18,20,15),Qt::AlignRight,"Hz");
}

void EchoPlot::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QFont font=this->font();font.setPixelSize(12);font.setFeature("tnum",1);p.setFont(font);
    const auto settings=parametersJson(parameters);const double amount=parameters.bypass?0.:settings.value("echo").toDouble()/100.;const double delay=settings.value("echoMs").toDouble(180.);
    const QRectF area=QRectF(rect()).adjusted(16,32,-24,-22);if(area.width()<1||area.height()<1)return;
    const auto x=[&](double ms){return area.left()+ms/1800.*area.width();};
    p.setPen(QColor("#b4b6aa"));p.drawText(QRect(0,0,width(),20),Qt::AlignLeft,amount==0?(parameters.bypass?"Echo bypassed":"Echo is off"):QString("Echo timing: a repeat every %1 ms").arg(delay,0,'f',0));
    p.setPen(QPen(QColor("#34382f"),1));p.drawLine(area.bottomLeft(),area.bottomRight());
    for(int ms:{0,600,1200,1800}){p.drawLine(QPointF(x(ms),area.bottom()),QPointF(x(ms),area.bottom()+4));p.setPen(QColor("#b4b6aa"));p.drawText(QRectF(x(ms)-24,area.bottom()+6,48,16),Qt::AlignHCenter,ms==0?"0":QString::number(ms)+" ms");p.setPen(QColor("#34382f"));}
    p.setPen(QPen(QColor("#a1a597"),4,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(x(0),area.bottom()),QPointF(x(0),area.top()));
    if(amount>0){for(int repeat=1;repeat<=3;++repeat){p.setPen(QPen(QColor("#e6ad66"),4,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(x(delay*repeat),area.bottom()),QPointF(x(delay*repeat),area.bottom()-area.height()*amount*std::pow(.35,repeat-1)));}}
    if(area.height()>36){p.setPen(QColor("#b4b6aa"));p.drawText(QRectF(area.left()+12,area.top(),100,16),Qt::AlignLeft,"Original voice");}
}

QWidget* Window::parameterControl(const QString& key,const QString& title,const QString& help,double low,double high,double step,double initial,const QString& suffix,bool vertical) {
    auto* widget=new QWidget; widget->setObjectName(key+"Control"); widget->setToolTip(help);
    auto* layout=new QVBoxLayout(widget); layout->setContentsMargins(vertical ? 12:0,0,vertical ? 12:0,0); layout->setSpacing(8);
    auto* spin=new QDoubleSpinBox; spin->setObjectName(key+"Value"); spin->setAccessibleName(title+" value");
    auto numericFont=spin->font();numericFont.setFeature("tnum",1);spin->setFont(numericFont);
    spin->setRange(low,high); spin->setSingleStep(step); spin->setDecimals(step<1 ? 1:0); spin->setSuffix(suffix); spin->setValue(initial);
    spin->setKeyboardTracking(false);spin->setGroupSeparatorShown(true);if(key=="highpass"||key=="gate"||key=="compression"||key=="drive"||key=="echo")spin->setSpecialValueText("Off"); spin->setFixedHeight(36); spin->setMinimumWidth(100); spin->setToolTip(help);
    const int original=static_cast<int>(std::round((initial-low)/step));
    auto* slider=new VoiceSlider(vertical ? Qt::Vertical:Qt::Horizontal,original,low<0&&high>0?std::optional<int>(static_cast<int>(std::round(-low/step))):std::nullopt); slider->setObjectName(key+"Slider"); slider->setAccessibleName(title);
    slider->setRange(0,static_cast<int>(std::round((high-low)/step))); slider->setValue(original); slider->setToolTip(help+QString("\nDouble-click to reset to %1%2.").arg(initial).arg(suffix));
    slider->setAccessibleDescription(slider->toolTip());
    values[key]=spin; sliders[key]=slider;
    if (vertical) {
        auto* titleLabel=text(title,"controlTitle"); titleLabel->setObjectName(key+"Title"); titleLabel->setAlignment(Qt::AlignCenter); layout->addWidget(titleLabel);
        auto* hint=text(help.section('.',0,0),"hint"); hint->setObjectName(key+"Hint"); hint->setAlignment(Qt::AlignCenter); layout->addWidget(hint);
        auto* faderRow=new QHBoxLayout; faderRow->setSpacing(8); auto* scale=new QWidget; scale->setObjectName(key+"Scale"); scale->setFixedWidth(28);
        auto* scaleLayout=new QVBoxLayout(scale); scaleLayout->setContentsMargins(0,0,0,0);
        for (const QString& tick:QStringList{"+18","0","−18"}) {auto* l=text(tick,"scale"); l->setAlignment(Qt::AlignRight); scaleLayout->addWidget(l); if (tick!="−18") scaleLayout->addStretch();}
        slider->setMinimumHeight(130); slider->setMaximumHeight(300); slider->setFixedWidth(35);
        faderRow->addStretch(); faderRow->addWidget(scale); faderRow->addWidget(slider); faderRow->addSpacing(30); faderRow->addStretch(); layout->addLayout(faderRow,1);
        layout->addWidget(spin,0,Qt::AlignHCenter); faderWidgets[key]=widget;faderRows[key]=faderRow;
    } else {
        auto* row=new QHBoxLayout; row->setSpacing(10);auto* label=text(title,"controlTitle");label->setObjectName(key+"Title");row->addWidget(label,1);row->addWidget(spin);parameterRows[key]=row; layout->addLayout(row);
        slider->setFixedHeight(26); layout->addWidget(slider);
        auto* hint=text(help.section('.',0,0)+".","hint"); hint->setWordWrap(true); layout->addWidget(hint);
    }
    connect(spin,&QDoubleSpinBox::editingFinished,this,[this]{QTimer::singleShot(0,this,[this]{syncControls();syncPresetSelection();});});
    connect(slider,&QSlider::valueChanged,this,[spin,low,step](int value){spin->setValue(low+value*step);});
    connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,key,slider,low,step](double value){
        QSignalBlocker block(slider); slider->setValue(static_cast<int>(std::round((value-low)/step))); if (!syncing) setParameter(key,value);
    });
    return widget;
}

Window::Window(Client& connection,bool demo):client(connection),demonstration(demo) {
    QApplication::setStyle(new ConsoleStyle);
    setWindowTitle("Murmur — Voice processor");setObjectName("murmurWindow");resize(1220,840);setMinimumSize(520,680);
    setStyleSheet(R"(
        QWidget { background:#171915; color:#efece4; font-family:"Noto Sans","Inter",sans-serif; font-size:14px; }
        QLabel#brand { font-size:28px; font-weight:700; letter-spacing:-1px; }
        QLabel#section,QLabel[role="section"] { font-size:20px; font-weight:600; }
        QLabel[role="hint"] { color:#b4b6aa; font-size:12px; }
        QLabel#scale { color:#a1a597; font-size:11px; }
        QLabel[role="controlTitle"] { font-size:14px; font-weight:600; }
        QLabel#status { color:#a6c29e; font-size:12px; }
        QFrame#rule { background:#34382f; border:0; }
        QComboBox,QDoubleSpinBox { background:#23261f; border:1px solid #555b4b; border-radius:4px; padding:5px 8px; selection-background-color:#705334; }
        QComboBox:hover,QDoubleSpinBox:hover { border-color:#848b76; }
        QComboBox:focus,QDoubleSpinBox:focus { border:1px solid #e6ad66; }
        QComboBox { min-height:24px; padding-right:24px; }
        QComboBox QAbstractItemView { background:#23261f; border:1px solid #737864; selection-background-color:#4e4333; padding:6px; }
        QPushButton,QToolButton { background:#292d24; border:1px solid #555b4b; border-radius:4px; padding:8px 12px; font-weight:600; }
        QPushButton:hover,QToolButton:hover { background:#353b2f; border-color:#848b76; }
        QPushButton:pressed,QToolButton:pressed { background:#414638; }
        QPushButton:focus,QToolButton:focus { border-color:#e6ad66; }
        QPushButton:disabled,QToolButton:disabled { color:#858b7b; background:#22251f; border-color:#34382f; }
        QPushButton#primary { background:#e6ad66; color:#252017; border-color:#e6ad66; }
        QPushButton#primary:hover { background:#f0bc7b; border-color:#f0bc7b; }
        QPushButton#primary:pressed { background:#cf9650; }
        QPushButton#primary:disabled { background:#4a4030; color:#b2a48a; border-color:#4a4030; }
        QToolButton#menu { background:transparent; border-color:transparent; font-size:22px; padding:1px 8px; }
        QToolButton#menu:focus { border-color:#e6ad66; }
        QToolButton#menu:hover { background:#292d24; }
        QToolButton#frequencyToggle { background:transparent; border:1px solid transparent; text-align:left; padding:4px 0; color:#b4b6aa; font-size:12px; font-weight:400; }
        QToolButton#frequencyToggle:hover { color:#e6ad66; }
        QToolButton#frequencyToggle:focus { border-color:#e6ad66; }
        QSlider::groove:horizontal { background:#4a5142; height:4px; border-radius:2px; }
        QSlider::sub-page:horizontal { background:#d5a363; border-radius:2px; }
        QSlider::handle:horizontal { background:#efece4; border:1px solid #efece4; width:13px; margin:-6px 0; border-radius:3px; }
        QSlider::handle:horizontal:hover { background:#e6ad66; border-color:#e6ad66; }
        QSlider::groove:vertical { background:#424938; width:5px; border-radius:2px; }
        QSlider::handle:vertical { background:#dcd9ce; border:1px solid #eeebe3; height:15px; margin:0 -9px; border-radius:3px; }
        QSlider::handle:vertical:hover { background:#e6ad66; border-color:#e6ad66; }
        QSlider:focus { border:1px dotted #d5a363; border-radius:3px; }
        QCheckBox { spacing:8px; font-size:13px; padding:4px 0; }
        QCheckBox:focus { color:#e6ad66; }
        QWidget:disabled { color:#858b7b; }
        QTabWidget::pane { border:0; padding-top:16px; }
        QTabBar::tab { background:#171915; padding:10px 18px; border-bottom:2px solid #34382f; color:#b4b6aa; font-weight:600; min-height:22px; }
        QTabBar::tab:selected { color:#e6ad66; border-bottom-color:#e6ad66; background:#20231d; }
        QTabBar::tab:hover { color:#efece4; background:#20231d; }
        QScrollArea { border:0; background:transparent; }
        QScrollBar:vertical { width:8px; background:#171915; margin:0; }
        QScrollBar::handle:vertical { background:#555b4b; min-height:28px; border-radius:4px; }
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
        QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }
        QMenu { background:#23261f; border:1px solid #737864; padding:5px; }
        QMenu::item { padding:9px 24px; }
        QMenu::item:selected { background:#414638; }
        QToolTip { background:#2c3027; color:#efece4; border:1px solid #737864; padding:6px; }
    )");
    outer=new QVBoxLayout(this);outer->setContentsMargins(24,20,24,16);outer->setSpacing(8);
    auto* header=new QHBoxLayout;header->setSpacing(12);header->addWidget(new Mark);header->addWidget(text("murmur","brand"));
    productLabel=text("Your voice. Your character.","hint");header->addSpacing(8);header->addWidget(productLabel);header->addStretch();
    service=text(demo ? "Example view · no audio":"Connecting to background service…","status");service->setObjectName("status");header->addWidget(service);
    auto* menuButton=new QToolButton;menuButton->setText("⋯");menuButton->setObjectName("menu");menuButton->setAccessibleName("Application menu");
    auto* menu=new QMenu(menuButton);auto* importPreset=menu->addAction("Import preset…",this,[this]{auto path=QFileDialog::getOpenFileName(this,"Import Murmur preset",{},"Murmur preset (*.json)");if(!path.isEmpty())loadFile(path);});importPreset->setObjectName("importPreset");
    menu->addAction("Export preset…",this,&Window::saveFile);menu->addSeparator();menu->addAction("How Murmur works",this,&Window::showHelp);menu->addAction("About Murmur",this,[this]{QMessageBox::about(this,"About Murmur",QString("Murmur %1\nNative voice controls for Linux\n\nShape your microphone for games, calls and streaming.\n\nReleased under the MIT License.").arg(QApplication::applicationVersion()));});menu->addSeparator();
    auto* stopService=menu->addAction("Stop background service",this,[this]{if(!demonstration)client.command("shutdown");});stopService->setObjectName("stopService");stopService->setEnabled(!demo);
    menuButton->setMenu(menu);menuButton->setPopupMode(QToolButton::InstantPopup);header->addWidget(menuButton);outer->addLayout(header);outer->addWidget(rule());

    routing=new QGridLayout;routing->setHorizontalSpacing(16);routing->setVerticalSpacing(6);routing->setColumnStretch(0,5);routing->setColumnStretch(2,4);
    routing->addWidget(text("Microphone","hint"),0,0);routing->addWidget(text("Your input in other apps","hint"),0,2);
    microphone=new QComboBox;microphone->setObjectName("microphone");microphone->setAccessibleName("Microphone input");microphone->setMinimumWidth(150);microphone->setMaximumWidth(560);
    microphone->setToolTip("Select your physical microphone. Your processed voice appears as Murmur Virtual Microphone in other applications.");routing->addWidget(microphone,1,0);
    auto* arrow=text("→","hint");arrow->setFixedWidth(18);arrow->setAlignment(Qt::AlignCenter);routing->addWidget(arrow,1,1);
    outputLabel=text("Murmur Virtual Microphone","controlTitle");outputLabel->setWordWrap(true);outputLabel->setToolTip("Choose this input device in Discord, OBS or your calling app.");routing->addWidget(outputLabel,1,2);
    startButton=new QPushButton("Start microphone");startButton->setObjectName("primary");startButton->setMinimumWidth(168);startButton->setAccessibleName("Start or stop virtual microphone");routing->addWidget(startButton,1,3);
    state=text("Start your microphone, then choose the Murmur input in your calling app.","hint");state->setWordWrap(true);routing->addWidget(state,2,0,1,4);outer->addLayout(routing);
    systemWarning=new QWidget;systemWarning->setObjectName("systemMuteWarning");auto* warningLayout=new QHBoxLayout(systemWarning);warningLayout->setContentsMargins(0,4,0,4);warningLayout->setSpacing(12);
    auto* warning=text("System input is muted. Other apps hear silence.","hint");warning->setStyleSheet("color:#e7b795;");warning->setWordWrap(true);warningLayout->addWidget(warning,1);
    unmuteSystem=new QPushButton("Unmute system input");unmuteSystem->setObjectName("unmuteSystem");unmuteSystem->setAccessibleName("Unmute Murmur in system audio");warningLayout->addWidget(unmuteSystem);systemWarning->hide();outer->addWidget(systemWarning);
    connect(unmuteSystem,&QPushButton::clicked,this,[this]{if(connected&&!demonstration)client.command("unmute-system");});
    outer->addWidget(rule());

    compactPresets=new QWidget;auto* presetRow=new QHBoxLayout(compactPresets);presetRow->setContentsMargins(0,0,0,0);presetRow->setSpacing(12);presetRow->addWidget(text("Preset","hint"));
    presets=new QComboBox;presets->setObjectName("presets");presets->setAccessibleName("Voice preset");presets->addItems(presetNames());presets->addItem("Custom");presets->setMinimumWidth(150);presetRow->addWidget(presets);
    presetHint=text("A starting point. Make it yours.","hint");presetRow->addWidget(presetHint);presetRow->addStretch();auto* reset=new QPushButton("Reset");reset->setObjectName("resetPreset");reset->setToolTip("Reset every voice and output setting to Clean. Mute and bypass stay unchanged.");presetRow->addWidget(reset);outer->addWidget(compactPresets);compactPresets->hide();
    workspace=new QHBoxLayout;workspace->setContentsMargins(0,4,0,4);workspace->setSpacing(24);
    presetRail=new QWidget;presetRail->setObjectName("presetRail");presetRail->setFixedWidth(172);auto* railLayout=new QVBoxLayout(presetRail);railLayout->setContentsMargins(0,0,0,0);railLayout->setSpacing(4);
    railLayout->addWidget(text("Sounds","section"));auto* railHint=text("Start with a character.","hint");railHint->setWordWrap(true);railHint->hide();railLayout->addSpacing(8);
    const QMap<QString,QString> descriptions={{"Clean","Original voice"},{"Warm","Fuller and softer"},{"Bright","Clear and present"},{"Telephone","Narrow and grainy"},{"Digital grit","Crunchy digital texture"},{"Deep","Lower and weightier"},{"Comms","Clearer team chat"}};
    for(const auto& name:presetNames()){auto* button=new PresetButton(name,descriptions.value(name,"Make it yours"));QString objectName=name;objectName.remove(' ');button->setObjectName("preset"+objectName);presetButtons[name]=button;railLayout->addWidget(button);connect(button,&QPushButton::clicked,this,[this,name]{usePreset(name);});}
    railLayout->addStretch();presetState=text("Clean preset","hint");presetState->setWordWrap(true);railLayout->addWidget(presetState);auto* save=new QPushButton("Export preset…");save->setObjectName("savePreset");connect(save,&QPushButton::clicked,this,&Window::saveFile);railLayout->addWidget(save);workspace->addWidget(presetRail);

    tone=new QWidget;tone->setObjectName("tonePanel");auto* toneLayout=new QVBoxLayout(tone);toneLayout->setContentsMargins(0,0,0,0);toneLayout->setSpacing(12);
    auto* toneHeading=new QHBoxLayout;toneTitle=text("Tone","section");toneHeading->addWidget(toneTitle);toneHeading->addStretch();toneGuide=text("+ boost   − cut","hint");toneHeading->addWidget(toneGuide);toneLayout->addLayout(toneHeading);
    plot=new ResponsePlot;plot->setObjectName("responsePlot");plot->setAccessibleName("Equalizer response graph, from bass to treble");plot->setMinimumHeight(152);plot->setMaximumHeight(240);toneLayout->addWidget(plot,2);
    faders=new QHBoxLayout;faders->setSpacing(0);
    faders->addWidget(parameterControl("bass","Bass","Body and warmth. Reduce to soften boom.",-18,18,.5,0," dB",true),1);
    faders->addWidget(parameterControl("mid","Middle","Speech presence. Reduce for a hollow tone.",-18,18,.5,0," dB",true),1);
    faders->addWidget(parameterControl("treble","Treble","Clarity and air. Reduce to soften consonants.",-18,18,.5,0," dB",true),1);toneLayout->addLayout(faders,3);
    frequenciesButton=new QToolButton;frequenciesButton->setObjectName("frequencyToggle");frequenciesButton->setText("Band frequencies");frequenciesButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);frequenciesButton->setArrowType(Qt::RightArrow);frequenciesButton->setCheckable(true);frequenciesButton->setAccessibleName("Show band frequency controls");toneLayout->addWidget(frequenciesButton,0,Qt::AlignLeft);
    frequencyPanel=new QWidget;frequencyPanel->setObjectName("frequencyControls");auto* frequencyRow=new QHBoxLayout(frequencyPanel);frequencyRow->setContentsMargins(0,0,0,0);frequencyRow->setSpacing(12);
    const struct {const char* key;const char* name;double low,high,value;} frequencies[]={{"bassHz","Bass corner",40,500,180},{"midHz","Middle center",250,4000,1200},{"trebleHz","Treble corner",2000,12000,4500}};
    for(const auto& field:frequencies){auto* column=new QVBoxLayout;column->setSpacing(4);auto* label=text(field.name,"hint");label->setAlignment(Qt::AlignCenter);column->addWidget(label);
        auto* spin=new QDoubleSpinBox;spin->setObjectName(QString(field.key)+"Value");spin->setAccessibleName(field.name);spin->setDecimals(0);spin->setRange(field.low,field.high);spin->setValue(field.value);spin->setSuffix(" Hz");spin->setKeyboardTracking(false);spin->setFixedHeight(36);spin->setMaximumWidth(144);
        auto numericFont=spin->font();numericFont.setFeature("tnum",1);spin->setFont(numericFont);
        spin->setToolTip(QString("Choose the frequency affected by %1. The response graph updates as you adjust it.").arg(field.name));column->addWidget(spin,0,Qt::AlignHCenter);values[field.key]=spin;
        connect(spin,&QDoubleSpinBox::editingFinished,this,[this]{QTimer::singleShot(0,this,[this]{syncControls();syncPresetSelection();});});
        connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,key=QString(field.key)](double value){if(!syncing)setParameter(key,value);});frequencyRow->addLayout(column,1);}
    toneLayout->addWidget(frequencyPanel);frequencyPanel->hide();connect(frequenciesButton,&QToolButton::toggled,this,[this](bool show){frequencyPanel->setVisible(show);if(show)tabs->setCurrentIndex(0);frequenciesButton->setArrowType(show?Qt::DownArrow:Qt::RightArrow);frequenciesButton->setAccessibleName(show?"Hide band frequency controls":"Show band frequency controls");});

    const auto module=[&](const QString& name,const QString& title,const QString& guidance,QWidget*& page){
        page=new QWidget;page->setObjectName(name+"Panel");auto* layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);layout->setSpacing(20);
        auto* heading=text(title,"section");heading->setObjectName(name+"Heading");layout->addWidget(heading);
        auto* guide=text(guidance,"hint");guide->setObjectName(name+"Guide");guide->setWordWrap(true);layout->addWidget(guide);
        auto* controls=new QWidget;controls->setObjectName(name+"Controls");auto* grid=new QGridLayout(controls);grid->setContentsMargins(0,0,0,0);grid->setHorizontalSpacing(36);grid->setVerticalSpacing(28);moduleGrids[name]=grid;layout->addWidget(controls);layout->addStretch();return grid;
    };
    const auto addControl=[&](const QString& page,const QString& key,const QString& title,const QString& help,double low,double high,double step,double initial,const QString& suffix){
        auto* control=parameterControl(key,title,help,low,high,step,initial,suffix);moduleKeys[page].append(key);const int index=static_cast<int>(moduleKeys[page].size())-1;moduleGrids[page]->addWidget(control,index/2,index%2);moduleGrids[page]->setColumnStretch(index%2,1);
    };
    module("texture","Character","Change how your voice feels. Keep the effect mix low for subtle character; turn it up for a stronger transformation.",texture);
    addControl("texture","pitch","Pitch","Lower or raise your voice. Semitones change pitch while preserving speaking speed. Large shifts may add artifacts.",-12,12,.5,0," st");
    addControl("texture","drive","Drive","Add warmth and distortion. Higher values make the voice rougher and more saturated.",0,100,1,0," %");
    addControl("texture","rate","Sample rate","Less detail, more grain. 48,000 Hz preserves detail. This changes texture, not speaking speed or hardware format.",1000,48000,100,48000," Hz");
    addControl("texture","bits","Bit depth","From clean to digital grit. 24 bits is clean. This changes the sound, not the recording format.",2,24,1,24," bits");
    textureTitle=texture->findChild<QLabel*>("textureHeading");textureGuide=texture->findChild<QLabel*>("textureGuide");
    textureNote=text("Rate and bits change texture. The microphone connection stays at 48 kHz.","hint");textureNote->setWordWrap(true);qobject_cast<QVBoxLayout*>(texture->layout())->addWidget(textureNote);
    module("cleanup","Cleanup","Make speech easier to hear. Start gently, then listen with your usual game or call running.",cleanup);
    addControl("cleanup","highpass","Low cut","Reduce rumble and desk noise. 20 Hz turns this filter off. Higher settings can thin your voice.",20,300,5,20," Hz");
    addControl("cleanup","gate","Noise gate","Quiet your microphone between words. Sound below this threshold fades out. This is not noise cancellation.",-80,-20,1,-80," dB");
    addControl("cleanup","compression","Compression","Even out loud and quiet speech. Higher values reduce loud peaks; output level stays under your control. No automatic gain boost.",0,100,1,0," %");
    module("space","Space","Add a delayed copy of your voice. A light touch works well in voice chat; a stronger echo is more theatrical.",space);
    addControl("space","echo","Echo amount","Blend in a delayed voice. Off keeps speech dry. Increase this for a more obvious repeat.",0,60,1,0," %");
    addControl("space","echoMs","Echo delay","Set the time before the repeat. Short delays add thickness; longer delays create a distinct echo.",80,600,10,180," ms");
    echoPlot=new EchoPlot;echoPlot->setObjectName("echoTiming");echoPlot->setAccessibleName("Configured echo timing");echoPlot->setAccessibleDescription("Diagram of the original voice and three configured repeats. Each repeat fades to 35 percent of the previous repeat.");echoPlot->setMinimumHeight(92);echoPlot->setMaximumHeight(160);
    qobject_cast<QVBoxLayout*>(space->layout())->insertWidget(3,echoPlot,1);
    const auto scroll=[&](QWidget* content,const QString& name){auto* area=new QScrollArea;area->setObjectName(name);area->setWidgetResizable(true);area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);area->setFrameShape(QFrame::NoFrame);area->setWidget(content);return area;};
    toneScroll=scroll(tone,"toneScroll");textureScroll=scroll(texture,"textureScroll");cleanupScroll=scroll(cleanup,"cleanupScroll");spaceScroll=scroll(space,"spaceScroll");
    divider=rule(Qt::Vertical);workspace->addWidget(divider);
    tabs=new ModuleTabs;tabs->setObjectName("compactTabs");tabs->setDocumentMode(true);tabs->setIconSize(QSize(20,20));tabs->tabBar()->setExpanding(false);tabs->tabBar()->setDrawBase(false);tabs->tabBar()->setAccessibleName("Voice control modules");
    tabs->addTab(toneScroll,moduleIcon(0),"Tone");tabs->addTab(textureScroll,moduleIcon(1),"Character");tabs->addTab(cleanupScroll,moduleIcon(2),"Cleanup");tabs->addTab(spaceScroll,moduleIcon(3),"Space");
    workspace->addWidget(tabs,1);outer->addLayout(workspace,1);outer->addWidget(rule());

    auto* footer=new QVBoxLayout;footer->setSpacing(6);auto* outputControls=new QHBoxLayout;outputControls->setSpacing(32);
    const auto outputColumn=[&](const QString& key,const QString& title,double low,double high,double step,double initial,const QString& suffix,const QString& help,LevelMeter*& meter){
        auto* column=new QWidget;auto* layout=new QVBoxLayout(column);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);auto* control=parameterControl(key,title,help,low,high,step,initial,suffix);
        if(auto* hint=control->findChild<QLabel*>("hint"))hint->hide();
        if(auto* controls=qobject_cast<QVBoxLayout*>(control->layout()))controls->setSpacing(0);
        values[key]->setFixedHeight(32);sliders[key]->setFixedHeight(20);layout->addWidget(control);
        meter=new LevelMeter(key=="mix"?"Microphone in":"Processed out");meter->setObjectName(key=="mix"?"inputMeter":"outputMeter");layout->addWidget(meter);return column;};
    outputControls->addWidget(outputColumn("mix","Effect mix",0,100,1,100," %","Blend original and processed voice. 0% is your original voice; 100% applies the full effect chain.",inputMeter),1);
    outputControls->addWidget(outputColumn("gain","Output level",-24,12,.5,0," dB","Final microphone volume. Lower this if the clipping guard activates.",outputMeter),1);footer->addLayout(outputControls);
    toggles=new QHBoxLayout;toggles->setSpacing(24);mute=new QCheckBox("Mute mic");mute->setObjectName("mute");mute->setAccessibleName("Mute microphone");mute->setToolTip("Silence the virtual microphone without disconnecting it.");
    bypass=new QCheckBox("Bypass effects");bypass->setObjectName("bypass");bypass->setToolTip("Send your original voice through. The microphone stays connected.");preview=new QCheckBox("Hear myself");preview->setObjectName("preview");preview->setToolTip("Monitor your processed voice. Use headphones to avoid feedback.");
    toggles->addWidget(mute);toggles->addWidget(bypass);toggles->addStretch();toggles->addWidget(preview);headphonesHint=text("Use headphones","hint");toggles->addWidget(headphonesHint);footer->addLayout(toggles);
    auto* note=new QHBoxLayout;note->setSpacing(12);footerNote=text("Closing this window keeps your microphone running.","hint");footerNote->setWordWrap(true);note->addWidget(footerNote,1);guard=text("Clipping guard ready","hint");note->addWidget(guard);footer->addLayout(note);outer->addLayout(footer);

    connect(reset,&QPushButton::clicked,this,[this]{usePreset("Clean");});
    connect(presets,&QComboBox::textActivated,this,[this](const QString& name){if(name!="Custom")usePreset(name);});
    connect(startButton,&QPushButton::clicked,this,[this]{
        if(demonstration)return;
        if(!connected){client.startService();return;}
        if(requested||running||starting)client.command("stop");
        else client.command("start",{{"source",microphone->currentData().toString()},{"monitor",preview->isChecked()}});
    });
    connect(microphone,&QComboBox::currentIndexChanged,this,[this]{refreshStart();});
    connect(mute,&QCheckBox::toggled,this,[this](bool enabled){if(!syncing){params.mute=enabled;refreshStart();if(!demonstration)client.command("set",{{"parameters",QJsonObject{{"mute",enabled}}}});}});
    connect(bypass,&QCheckBox::toggled,this,[this](bool enabled){if(!syncing){params.bypass=enabled;plot->parameters=params;plot->update();echoPlot->parameters=params;echoPlot->update();refreshStart();if(!demonstration)client.command("set",{{"parameters",QJsonObject{{"bypass",enabled}}}});}});
    connect(preview,&QCheckBox::toggled,this,[this](bool enabled){if(!syncing&&!demonstration&&connected)client.command("monitor",{{"enabled",enabled}});});
    if (!demo) {
        connect(&client,&Client::connectionChanged,this,[this](bool live){connected=live; if(!live){running=false;ready=false;requested=false;starting=false;inputMeter->setLevel(-60);outputMeter->setLevel(-60);state->setText("Background service is offline. Start service to reconnect.");state->setStyleSheet("");}refreshStart();});
        connect(&client,&Client::stateReceived,this,&Window::receiveState);
        connect(&client,&Client::metersReceived,this,[this](float in,float out,bool limited){if(!running){inputMeter->setLevel(-60);outputMeter->setLevel(-60);return;}inputMeter->setLevel(in);outputMeter->setLevel(out);guard->setText(limited ? "Guard active · lower output":"Clipping guard ready");guard->setStyleSheet(limited ? "color:#e6ad66;":"");});
        connect(&client,&Client::problem,this,[this](const QString& error){state->setText(error);state->setStyleSheet("color:#e7a496;");});
    } else {
        microphone->addItem("Studio microphone · example","example"); running=true;ready=true;connected=true;
        state->setText("Choose Murmur Virtual Microphone in Discord, OBS or your calling app."); inputMeter->setLevel(-18.4f); outputMeter->setLevel(-21.2f);
        footerNote->setText("Example settings and levels. No microphone is connected.");
    }
    QWidget* previous=microphone;
    const auto tabAfter=[&](QWidget* next){QWidget::setTabOrder(previous,next);previous=next;};
    tabAfter(startButton);tabAfter(unmuteSystem);tabAfter(presets);tabAfter(reset);
    for(const auto& name:presetNames())tabAfter(presetButtons[name]);
    tabAfter(save);
    tabAfter(tabs->tabBar());
    for(const auto& key:QStringList{"bass","mid","treble"}){tabAfter(sliders[key]);tabAfter(values[key]);}
    tabAfter(frequenciesButton);
    for(const auto& key:QStringList{"bassHz","midHz","trebleHz"})tabAfter(values[key]);
    for(const auto& key:QStringList{"pitch","drive","rate","bits","highpass","gate","compression","echo","echoMs","mix","gain"}){tabAfter(values[key]);tabAfter(sliders[key]);}
    tabAfter(mute);tabAfter(bypass);tabAfter(preview);tabAfter(menuButton);
    setFocusPolicy(Qt::StrongFocus);setFocus(Qt::OtherFocusReason);
    syncControls();refreshStart();syncPresetSelection();
}

void Window::setParameter(const QString& key,double value) {
    murmur::Parameters next; QString error;
    if(!applyParameterPatch({{key,value}},params,next,error))return;
    params=next;plot->parameters=params;plot->update();echoPlot->parameters=params;echoPlot->update();updateReadouts(); {QSignalBlocker blocker(presets);presets->setCurrentText("Custom");}
    syncPresetSelection();if(!demonstration)client.command("set",{{"parameters",QJsonObject{{key,value}}}});
}
void Window::applyParameters(const murmur::Parameters& next,bool custom) {
    params=next;syncControls();if(custom){QSignalBlocker block(presets);presets->setCurrentText("Custom");}
    syncPresetSelection();if(!demonstration)client.command("set",{{"parameters",parametersJson(params)}});
}
void Window::usePreset(const QString& name) {
    auto p=presetParameters(name);p.mute=params.mute;p.bypass=params.bypass;applyParameters(p,false);QSignalBlocker block(presets);presets->setCurrentText(name);syncPresetSelection();
}
void Window::syncControls() {
    syncing=true;const auto json=parametersJson(params);
    for(auto& [key,spin]:values){const double incoming=json.value(key).toDouble();if(!spin->hasFocus()&&spin->value()!=incoming)spin->setValue(incoming);if(sliders.contains(key)&&(!spin->hasFocus()||spin->value()==incoming)){auto* slider=sliders[key];QSignalBlocker block(slider);slider->setValue(static_cast<int>(std::round((spin->value()-spin->minimum())/spin->singleStep())));}}
    mute->setChecked(params.mute);bypass->setChecked(params.bypass);plot->parameters=params;plot->update();echoPlot->parameters=params;echoPlot->update();updateReadouts();syncing=false;
}
void Window::updateReadouts() {
    const auto settings=parametersJson(params);const double amount=settings.value("compression").toDouble();
    const QString compressionText=amount>0?QString("%1:1 above −18 dB. No automatic gain boost.").arg(1+amount*.05,0,'f',2):QString("Compression is off. Loud peaks stay unchanged.");
    values["compression"]->parentWidget()->findChild<QLabel*>("hint")->setText(compressionText);
    const double gate=settings.value("gate").toDouble(-80);values["gate"]->parentWidget()->findChild<QLabel*>("hint")->setText(gate<=-80?"Gate is off. Background sound passes through.":QString("Below %1 dB, pauses fade out. Speech opens the gate.").arg(gate,0,'f',0));
    const double cutoff=settings.value("highpass").toDouble(20);values["highpass"]->parentWidget()->findChild<QLabel*>("hint")->setText(cutoff<=20?"Low cut is off. All bass frequencies pass through.":QString("Reduce rumble below %1 Hz. Higher values thin your voice.").arg(cutoff,0,'f',0));
}
void Window::syncPresetSelection() {
    QString chosen="Custom";
    for(const auto& name:presetNames())if(parametersJson(presetParameters(name),false)==parametersJson(params,false)){chosen=name;break;}
    QSignalBlocker block(presets);presets->setCurrentText(chosen);
    for(auto& [name,button]:presetButtons){QSignalBlocker guard(button);button->setChecked(name==chosen);button->update();}
    if(presetState){presetState->setText("Custom sound");presetState->setVisible(chosen=="Custom");}
}
void Window::receiveState(const QJsonObject& snapshot) {
    backgroundWarning=snapshot.value("backgroundWarning").toString();
    systemMuteKnown=snapshot.value("systemMuteKnown").toBool();systemMuted=snapshot.value("systemMuted").toBool();
    connected=true;ready=snapshot.value("audioReady").toBool();running=snapshot.value("running").toBool();requested=snapshot.value("requested").toBool();starting=snapshot.value("starting").toBool();
    const auto source=snapshot.value("source").toString();
    const QString previous=microphone->currentData().toString();
    {QSignalBlocker block(microphone);const auto sources=snapshot.value("sources").toArray();bool changed=sources.size()!=microphone->count();
    for(int i=0;!changed&&i<sources.size();++i){const auto device=sources[i].toObject();changed=microphone->itemText(i)!=device.value("description").toString()||microphone->itemData(i).toString()!=device.value("name").toString();}
    if(changed){microphone->clear();for(const auto& item:sources){const auto device=item.toObject();microphone->addItem(device.value("description").toString(),device.value("name").toString());}}
    const int index=microphone->findData(requested||running ? source:(previous.isEmpty() ? source:previous));if(index>=0&&microphone->currentIndex()!=index)microphone->setCurrentIndex(index);}
    murmur::Parameters incoming;QString error;
    if(applyParameterPatch(snapshot.value("parameters").toObject(),params,incoming,error)) {
        const bool changed=parametersJson(params)!=parametersJson(incoming);params=incoming;syncControls();
        if(changed){QSignalBlocker block(presets);QString matching="Custom";for(const auto& name:presetNames())if(parametersJson(presetParameters(name),false)==parametersJson(params,false)){matching=name;break;}presets->setCurrentText(matching);}
    }
    {QSignalBlocker block(preview);preview->setChecked(snapshot.value("monitor").toBool());}
    const QString failure=snapshot.value("error").toString();
    state->setStyleSheet(failure.isEmpty() ? "":"color:#e7a496;");
    state->setText(!failure.isEmpty() ? failure:running ? "Choose Murmur Virtual Microphone as the input in your calling app.":starting ? "Connecting your microphone…":requested ? "Waiting for the selected microphone. You can stop this request at any time.":!ready ? "Waiting for PipeWire. Start your audio session to continue.":microphone->count()==0 ? "No input microphones found. Connect a microphone to begin.":"Ready when you are. Start the microphone, then select the Murmur input in your app.");
    if(!running){inputMeter->setLevel(-60);outputMeter->setLevel(-60);}syncPresetSelection();refreshStart();
}
void Window::refreshStart() {
    if(!startButton)return;
    startButton->setText(demonstration ? "Example only":!connected ? "Start service":requested||running||starting ? "Stop microphone":"Start microphone");
    toneGuide->setText(params.bypass?"Effects bypassed":"+ boost   − cut");
    plot->setAccessibleDescription(params.bypass?"Effects bypassed. Original voice has a flat response.":"Read-only Tone EQ response of the Bass, Middle and Treble controls. This graph excludes Cleanup and Character effects. Adjust the sliders below to change it.");
    startButton->setEnabled(!demonstration&&(!connected||requested||running||starting||(ready&&microphone->count()>0)));
    microphone->setEnabled(!demonstration&&connected&&!requested&&!running&&!starting&&microphone->count()>0);
    if(!demonstration){service->setText(!connected ? "○  Service offline":running ? (systemMuteKnown&&systemMuted ? "●  System input muted":params.mute ? "●  Microphone muted":params.bypass ? "●  Effects bypassed":"●  Microphone active"):starting ? "●  Connecting":requested ? "○  Waiting for microphone":"●  Background service ready");service->setStyleSheet(!connected ? "color:#b4b6aa;":systemMuteKnown&&systemMuted ? "color:#e6ad66;":"color:#a6c29e;");}
    microphone->setToolTip(running||requested||starting ? "Stop the microphone to choose another input.":"Select your physical microphone. Your processed voice appears as Murmur Virtual Microphone in other applications.");
    systemWarning->setVisible(!demonstration&&connected&&systemMuteKnown&&systemMuted);unmuteSystem->setEnabled(connected&&!demonstration);
    updateToneDensity();
    for(auto& [name,button]:presetButtons){Q_UNUSED(name);button->setEnabled(demonstration||connected);}
    if(!running&&!demonstration){guard->setText(connected ? "Clipping guard · mic stopped":"Clipping guard · offline");guard->setStyleSheet("");}
    else if(guard->text().contains("stopped")||guard->text().contains("offline")){guard->setText("Clipping guard ready");guard->setStyleSheet("");}
    if(auto* action=findChild<QAction*>("importPreset"))action->setEnabled(demonstration||connected);
    if(auto* action=findChild<QAction*>("stopService"))action->setEnabled(!demonstration&&connected);
    if(!demonstration){footerNote->setText(!connected ? "Reconnect the background service to adjust your voice.":running&&!backgroundWarning.isEmpty()?backgroundWarning:running||requested ? "Closing this window keeps your microphone running.":"Microphone stopped · your settings are saved.");footerNote->setStyleSheet(connected&&running&&!backgroundWarning.isEmpty()?"color:#e6ad66;":"");}
    for(auto& [key,spin]:values){Q_UNUSED(key);spin->setEnabled(demonstration||connected);}for(auto& [key,slider]:sliders){Q_UNUSED(key);slider->setEnabled(demonstration||connected);}presets->setEnabled(demonstration||connected);if(auto* reset=findChild<QPushButton*>("resetPreset"))reset->setEnabled(demonstration||connected);
    tone->setEnabled(demonstration||connected);texture->setEnabled(demonstration||connected);cleanup->setEnabled(demonstration||connected);space->setEnabled(demonstration||connected);mute->setEnabled(demonstration||connected);bypass->setEnabled(demonstration||connected);preview->setEnabled(demonstration||connected);
}
void Window::updateToneDensity() {
    const bool warning=narrow&&(!systemWarning->isHidden()||(connected&&running&&!backgroundWarning.isEmpty()));
    // On short windows, recovery and editable controls take priority over the readout.
    plot->setVisible(!(warning&&height()<700));
    plot->setMinimumHeight(narrow?44:100);
    plot->setMaximumHeight(narrow?60:160);
    if(auto* layout=qobject_cast<QVBoxLayout*>(tone->layout()))layout->setSpacing(narrow?4:8);
    faders->setSpacing(narrow?(warning?2:4):0);
}
void Window::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);if(!workspace||!tabs)return;
    const bool compact=width()<920;
    narrow=compact;presetRail->setVisible(!compact);divider->setVisible(!compact);compactPresets->setVisible(compact);
    const bool small=width()<820;
    productLabel->setVisible(!small);presetHint->setVisible(!small);headphonesHint->setVisible(!small);toggles->setSpacing(small?16:24);
    routing->removeWidget(startButton);routing->removeWidget(state);
    if(small){routing->addWidget(startButton,2,2);routing->addWidget(state,2,0,1,2);}
    else{routing->addWidget(startButton,1,3);routing->addWidget(state,2,0,1,4);}
    outer->setContentsMargins(small?16:24,small?12:20,small?16:24,small?12:16);outer->setSpacing(8);workspace->setSpacing(compact?0:24);
    toneTitle->setVisible(false);toneGuide->setVisible(!compact);textureNote->setVisible(!compact);
    for(const auto& name:QStringList{"texture","cleanup","space"}){
        QWidget* page=name=="texture"?texture:name=="cleanup"?cleanup:space;
        page->findChild<QLabel*>(name+"Heading")->setVisible(!compact);
        page->findChild<QLabel*>(name+"Guide")->setVisible(!(compact&&height()<760));
        if(auto* layout=qobject_cast<QVBoxLayout*>(page->layout()))layout->setSpacing(compact?10:20);
        auto* grid=moduleGrids[name];grid->setHorizontalSpacing(36);grid->setVerticalSpacing(compact?12:28);
        int i=0;for(const auto& key:moduleKeys[name]){
            auto* control=values[key]->parentWidget();grid->removeWidget(control);grid->addWidget(control,compact||name=="cleanup"?i:i/2,compact||name=="cleanup"?0:i%2);++i;
            if(auto* hint=control->findChild<QLabel*>("hint"))hint->setVisible(!(compact&&height()<760));
            auto* layout=qobject_cast<QVBoxLayout*>(control->layout());auto* row=parameterRows[key];auto* slider=sliders[key];
            layout->removeWidget(slider);row->removeWidget(slider);
            if(compact||name=="cleanup")row->insertWidget(1,slider,1);else layout->insertWidget(1,slider);
            auto* label=control->findChild<QLabel*>(key+"Title");label->setMinimumWidth(compact||name=="cleanup"?104:0);label->setMaximumWidth(compact||name=="cleanup"?104:16777215);row->setStretch(0,compact||name=="cleanup"?0:1);
            values[key]->setFixedWidth(key=="rate"?128:112);layout->setSpacing(compact?0:8);slider->setFixedHeight(compact?26:26);
        }
        grid->setColumnStretch(0,1);grid->setColumnStretch(1,compact||name=="cleanup"?0:1);
    }
    echoPlot->setMinimumHeight(compact?80:120);echoPlot->setMaximumHeight(compact?96:180);
    tabs->setStyleSheet(compact?"QTabBar::tab {padding:6px 10px;} QTabWidget::pane {padding-top:8px;}":"");
    faders->setDirection(compact?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    for(const auto& key:QStringList{"bass","mid","treble"}){
        auto* control=faderWidgets[key];control->setMinimumHeight(compact?36:0);auto* layout=qobject_cast<QVBoxLayout*>(control->layout());auto* slider=sliders[key];
        layout->setDirection(compact?QBoxLayout::LeftToRight:QBoxLayout::TopToBottom);layout->setContentsMargins(compact?0:8,0,compact?0:8,0);layout->setSpacing(8);
        auto* title=control->findChild<QLabel*>(key+"Title");title->setMinimumWidth(compact?54:0);title->setMaximumWidth(compact?54:16777215);title->setAlignment(compact?Qt::AlignLeft|Qt::AlignVCenter:Qt::AlignCenter);
        control->findChild<QLabel*>(key+"Hint")->setVisible(!compact);control->findChild<QWidget*>(key+"Scale")->setVisible(!compact);
        slider->setOrientation(compact?Qt::Horizontal:Qt::Vertical);slider->setMinimumWidth(compact?40:35);slider->setMaximumWidth(compact?16777215:35);slider->setMinimumHeight(compact?26:130);slider->setMaximumHeight(compact?26:300);slider->setSizePolicy(compact?QSizePolicy::Expanding:QSizePolicy::Fixed,compact?QSizePolicy::Fixed:QSizePolicy::Expanding);
        faderRows[key]->setStretch(2,compact?1:0);values[key]->setMinimumWidth(102);values[key]->setMaximumWidth(compact?108:126);
    }
    updateToneDensity();
}
bool Window::loadFile(const QString& path) {
    murmur::Parameters loaded;QString error;if(!readParameters(path,loaded,error)){QMessageBox::warning(this,"Preset not loaded",error);return false;}loaded.mute=params.mute;loaded.bypass=params.bypass;applyParameters(loaded);return true;
}
void Window::saveFile() {
    auto path=QFileDialog::getSaveFileName(this,"Export Murmur preset","my-voice.json","Murmur preset (*.json)");if(path.isEmpty())return;if(!path.endsWith(".json"))path+=".json";QString error;if(!writeParameters(path,params,error))QMessageBox::warning(this,"Preset not saved",error);
}
void Window::showHelp() {
    QMessageBox::information(this,"Your voice, your controls",QString::fromUtf8(
        "1. Choose your physical microphone and start it.\n2. In Discord, OBS or your calling app, choose Murmur Virtual Microphone.\n\n"
        "Tone: Bass adds body, Middle changes speech presence, Treble adds clarity. The small frequency controls choose where each band acts.\n\n"
        "Character: Pitch lowers or raises your voice. Drive adds saturation. Sample rate reduces detail without slowing speech; bit depth adds digital grit. These effects do not change hardware format.\n\n"
        "Cleanup: Low cut removes rumble, Noise gate quiets pauses, and Compression evens out speech. The gate is not noise cancellation. Compression does not add automatic gain.\n\n"
        "Space: Echo mixes in a delayed voice. Echo delay sets the repeat timing.\n\n"
        "Effect mix blends the original and processed voice. Output level adjusts the final volume. The clipping guard helps keep boosted audio under control.\n\n"
        "Hear myself sends your processed voice to your default output. Use headphones.\n\n"
        "Murmur’s background service starts when you open the app. Native audio continues when the window closes. Flatpak needs desktop background permission; keep the window open if the status asks you to. Stop microphone removes the virtual input; Stop background service in the menu shuts the worker down. Murmur does not start at login."));
}
