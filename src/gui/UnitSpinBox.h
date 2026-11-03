#pragma once

#include <QDoubleSpinBox>
#include <QString>

#include <algorithm>
#include <vector>

// QDoubleSpinBox that stores lengths in millimetres but displays them in the
// application unit (mm or inch). Only spin boxes whose suffix is exactly
// " mm" are converted; angle, speed and count spin boxes behave normally.
class UnitSpinBox : public QDoubleSpinBox {
public:
    explicit UnitSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        instances().push_back(this);
        applyConversion();
    }

    ~UnitSpinBox() override {
        auto& list = instances();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    void setRange(double minimum, double maximum) {
        rawMin_ = minimum;
        rawMax_ = maximum;
        applyConversion();
    }

    void setValue(double value) {
        rawValue_ = value;
        applyConversion();
    }

    double value() const {
        return length_ ? rawValue_ : QDoubleSpinBox::value();
    }

    void setSingleStep(double step) {
        rawStep_ = step;
        applyConversion();
    }

    void setDecimals(int decimals) {
        rawDecimals_ = decimals;
        applyConversion();
    }

    void setSuffix(const QString& suffix) {
        rawSuffix_ = suffix;
        length_ = suffix.trimmed().compare("mm", Qt::CaseInsensitive) == 0;
        applyConversion();
    }

    static void setDisplayInch(bool inch) {
        displayInch_ = inch;
        for (UnitSpinBox* spin : instances()) spin->applyConversion();
    }
    static bool displayInch() { return displayInch_; }

private:
    static std::vector<UnitSpinBox*>& instances() {
        static std::vector<UnitSpinBox*> list;
        return list;
    }

    void applyConversion() {
        if (!length_) {
            QDoubleSpinBox::setDecimals(rawDecimals_);
            QDoubleSpinBox::setRange(rawMin_, rawMax_);
            QDoubleSpinBox::setSingleStep(rawStep_);
            QDoubleSpinBox::setValue(rawValue_);
            QDoubleSpinBox::setSuffix(rawSuffix_);
            return;
        }
        const double factor = displayInch_ ? 25.4 : 1.0;
        QDoubleSpinBox::setDecimals(displayInch_ ? rawDecimals_ + 4 : rawDecimals_);
        QDoubleSpinBox::setRange(rawMin_ / factor, rawMax_ / factor);
        QDoubleSpinBox::setSingleStep(rawStep_ / factor);
        QDoubleSpinBox::setValue(rawValue_ / factor);
        QDoubleSpinBox::setSuffix(displayInch_ ? " in" : " mm");
    }

    double rawMin_ = 0.0;
    double rawMax_ = 99.99;
    double rawValue_ = 0.0;
    double rawStep_ = 1.0;
    int rawDecimals_ = 2;
    QString rawSuffix_;
    bool length_ = false;
    static inline bool displayInch_ = false;
};
