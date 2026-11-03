/**
 * @file UnitSpinBox.h
 * @brief QDoubleSpinBox que guarda longitudes en mm y las muestra en mm/in.
 */
#pragma once

#include <QDoubleSpinBox>
#include <QString>

#include <algorithm>
#include <vector>

/**
 * @brief Campo numérico con unidad de longitud conmutable.
 *
 * Internamente guarda el valor en milímetros. Cuando el sufijo es exactamente
 * `" mm"` muestra el valor en la unidad activa (mm o pulgadas) y convierte los
 * límites, el paso y los decimales. Ángulos, RPM, avances y porcentajes se
 * comportan como un `QDoubleSpinBox` normal.
 *
 * Un registro estático permite actualizar todos los campos abiertos al cambiar
 * de unidad sin reiniciar la aplicación.
 */
class UnitSpinBox : public QDoubleSpinBox {
public:
    /// @brief Crea el campo y lo registra para el cambio de unidad.
    explicit UnitSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        instances().push_back(this);
        applyConversion();
    }

    /// @brief Quita el campo del registro.
    ~UnitSpinBox() override {
        auto& list = instances();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    /// @brief Fija el rango en milímetros.
    void setRange(double minimum, double maximum) {
        rawMin_ = minimum;
        rawMax_ = maximum;
        applyConversion();
    }

    /// @brief Fija el valor en milímetros.
    void setValue(double value) {
        rawValue_ = value;
        applyConversion();
    }

    /// @return Valor en milímetros (aunque se muestre en pulgadas).
    double value() const {
        return length_ ? rawValue_ : QDoubleSpinBox::value();
    }

    /// @brief Fija el incremento en milímetros.
    void setSingleStep(double step) {
        rawStep_ = step;
        applyConversion();
    }

    /// @brief Fija los decimales en milímetros.
    void setDecimals(int decimals) {
        rawDecimals_ = decimals;
        applyConversion();
    }

    /// @brief Define el sufijo; `" mm"` activa la conversión.
    void setSuffix(const QString& suffix) {
        rawSuffix_ = suffix;
        length_ = suffix.trimmed().compare("mm", Qt::CaseInsensitive) == 0;
        applyConversion();
    }

    /// @brief Cambia la unidad de todos los campos registrados.
    static void setDisplayInch(bool inch) {
        displayInch_ = inch;
        for (UnitSpinBox* spin : instances()) spin->applyConversion();
    }
    /// @return `true` si la unidad activa es pulgadas.
    static bool displayInch() { return displayInch_; }

private:
    /// Lista de campos vivos para actualizar la unidad en caliente.
    static std::vector<UnitSpinBox*>& instances() {
        static std::vector<UnitSpinBox*> list;
        return list;
    }

    /// Aplica la unidad activa a los valores crudos en mm.
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
