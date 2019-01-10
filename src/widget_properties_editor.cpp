/****************************************************************************
** Copyright (c) 2019, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "widget_properties_editor.h"

#include "document.h"
#include "document_item.h"
#include "gui_application.h"
#include "gui_document.h"
#include "gpx_document_item.h"
#include "options.h"
#include "string_utils.h"
#include "unit_system.h"
#include "ui_widget_properties_editor.h"
#include <fougtools/occtools/qt_utils.h>
#include <fougtools/qttools/gui/qwidget_utils.h>

#include <QtGui/QPainter>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QToolButton>

#include <functional>
#include <unordered_map>

namespace Mayo {

namespace Internal {

class PanelEditor : public QWidget {
public:
    PanelEditor(QWidget* parent = nullptr)
        : QWidget(parent)
    {}

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);

        // This is needed by the "classic" theme, force a painted background
        const QRect frame = this->frameGeometry();
        const QRect surface(0, 0, frame.width(), frame.height());
        const QColor panelColor = palette().color(QPalette::Base);
        painter.fillRect(surface, panelColor);

        QStyleOption option;
        option.initFrom(this);
        option.state |= QStyle::State_HasFocus;
        this->style()->drawPrimitive(QStyle::PE_FrameLineEdit, &option, &painter, this);
    }
};

static QPixmap colorSquarePixmap(const QColor& c, int sideLen = 16)
{
    QPixmap pixColor(sideLen, sideLen);
    pixColor.fill(c);
    return pixColor;
}

static QWidget* hSpacerWidget(QWidget* parent, int stretch = 1)
{
    auto widget = new QWidget(parent);
    QSizePolicy sp = widget->sizePolicy();
    sp.setHorizontalStretch(stretch);
    widget->setSizePolicy(sp);
    return widget;
}

static QString yesNoString(bool on)
{
    return on ? WidgetPropertiesEditor::tr("Yes") : WidgetPropertiesEditor::tr("No");
}

static QString toStringDHMS(QuantityTime time)
{
    const double duration_s = UnitSystem::seconds(time);
    const double days = duration_s / 86400.;
    const int dayCount = std::floor(days);
    const double hours = (days - dayCount) * 24;
    const int hourCount = std::floor(hours);
    const double mins = (hours - hourCount) * 60;
    const int minCount = std::floor(mins);
    const double secs = (mins - minCount) * 60;
    const int secCount = std::floor(secs);
    QString text;
    if (dayCount > 0)
        text += WidgetPropertiesEditor::tr("%1d ").arg(dayCount);
    if (hourCount > 0)
        text += WidgetPropertiesEditor::tr("%1h ").arg(hourCount);
    if (minCount > 0)
        text += WidgetPropertiesEditor::tr("%1min ").arg(minCount);
    if (secCount > 0)
        text += WidgetPropertiesEditor::tr("%1s").arg(secCount);
    return text.trimmed();
}

static UnitSystem::TranslateResult unitTranslate(const BasePropertyQuantity* prop)
{
    if (prop->quantityUnit() == Unit::Angle) {
        auto propAngle = static_cast<const PropertyAngle*>(prop);
        return UnitSystem::degrees(propAngle->quantity());
    }
    return Options::instance()->unitSystemTranslate(
                prop->quantityValue(), prop->quantityUnit());
}

static QString propertyValueText(const PropertyBool* prop) {
    return yesNoString(prop->value());
}

static QString propertyValueText(const PropertyInt* prop) {
    return Options::instance()->locale().toString(prop->value());
}

static QString propertyValueText(const PropertyDouble* prop) {
    return StringUtils::text(prop->value(), Options::instance()->defaultTextOptions());
}

static QString propertyValueText(const PropertyQByteArray* prop) {
    return QString::fromUtf8(prop->value());
}

static QString propertyValueText(const PropertyQString* prop) {
    return prop->value();
}

static QString propertyValueText(const PropertyQDateTime* prop) {
    return Options::instance()->locale().toString(prop->value());
}

static QString propertyValueText(const PropertyOccColor* prop) {
    return StringUtils::text(prop->value());
}

static QString propertyValueText(const PropertyOccPnt* prop) {
    return StringUtils::text(prop->value(), Options::instance()->defaultTextOptions());
}

static QString propertyValueText(const PropertyOccTrsf* prop) {
    return StringUtils::text(prop->value(), Options::instance()->defaultTextOptions());
}

static QString propertyValueText(const PropertyEnumeration* prop)
{
    const auto& enumMappings = prop->enumeration().mappings();
    for (const Enumeration::Mapping& mapping : enumMappings) {
        if (mapping.value == prop->value())
            return mapping.string;
    }
    return QString();
}

static QString propertyValueText(const BasePropertyQuantity* prop)
{
    if (prop->quantityUnit() == Unit::Time) {
        auto propTime = static_cast<const PropertyTime*>(prop);
        return toStringDHMS(propTime->quantity());
    }
    const UnitSystem::TranslateResult trRes = unitTranslate(prop);
    return WidgetPropertiesEditor::tr("%1%2")
            .arg(StringUtils::text(trRes.value, Options::instance()->defaultTextOptions()))
            .arg(trRes.strUnit);
}

static QString propertyValueText(
        const BasePropertyQuantity* prop,
        const WidgetPropertiesEditor::UnitTranslation& unitTr)
{
    const double trValue = prop->quantityValue() * unitTr.factor;
    return WidgetPropertiesEditor::tr("%1%2")
            .arg(StringUtils::text(trValue, Options::instance()->defaultTextOptions()))
            .arg(unitTr.strUnit);
}

static QWidget* createPropertyEditor(BasePropertyQuantity* prop, QWidget* parent)
{
    auto editor = new QDoubleSpinBox(parent);
    const UnitSystem::TranslateResult trRes = unitTranslate(prop);
    editor->setSuffix(QString::fromUtf8(trRes.strUnit));
    editor->setDecimals(Options::instance()->unitSystemDecimals());
    const double rangeMin =
            prop->constraintsEnabled() ?
                prop->minimum() : std::numeric_limits<double>::min();
    const double rangeMax =
            prop->constraintsEnabled() ?
                prop->maximum() : std::numeric_limits<double>::max();
    editor->setRange(rangeMin, rangeMax);
    editor->setValue(trRes.value);
    auto signalValueChanged =
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged);
    QObject::connect(editor, signalValueChanged, [=](double value) {
        const double f = trRes.factor;
        value = qFuzzyCompare(f, 1.) ? value : value * f;
        prop->setQuantityValue(value);
    });
    return editor;
}

static QWidget* createPanelEditor(QWidget* parent)
{
    auto frame = new PanelEditor(parent);
    auto layout = new QHBoxLayout(frame);
    layout->setContentsMargins(2, 0, 0, 0);
    return frame;
}

static QWidget* createPropertyEditor(PropertyBool* prop, QWidget* parent)
{
    auto frame = createPanelEditor(parent);
    auto editor = new QCheckBox(frame);
    frame->layout()->addWidget(editor);
    editor->setText(yesNoString(prop->value()));
    editor->setChecked(prop->value());
    QObject::connect(editor, &QCheckBox::toggled, [=](bool on) {
        editor->setText(yesNoString(on));
        prop->setValue(on);
    });
    return frame;
}

static QWidget* createPropertyEditor(PropertyInt* prop, QWidget* parent)
{
    auto editor = new QSpinBox(parent);
    if (prop->constraintsEnabled()) {
        editor->setRange(prop->minimum(), prop->maximum());
        editor->setSingleStep(prop->singleStep());
    }
    editor->setValue(prop->value());
    auto signalValueChanged =
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged);
    QObject::connect(editor, signalValueChanged, [=](int val) {
        prop->setValue(val);
    });
    return editor;
}

static QWidget* createPropertyEditor(PropertyDouble* prop, QWidget* parent)
{
    auto editor = new QDoubleSpinBox(parent);
    if (prop->constraintsEnabled()) {
        editor->setRange(prop->minimum(), prop->maximum());
        editor->setSingleStep(prop->singleStep());
    }
    editor->setValue(prop->value());
    editor->setDecimals(Options::instance()->unitSystemDecimals());
    auto signalValueChanged =
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged);
    QObject::connect(editor, signalValueChanged, [=](double val) {
        prop->setValue(val);
    });
    return editor;
}

static QWidget* createPropertyEditor(PropertyQString* prop, QWidget* parent)
{
    auto editor = new QLineEdit(parent);
    editor->setText(prop->value());
    QObject::connect(editor, &QLineEdit::textChanged, [=](const QString& text) {
        prop->setValue(text);
    });
    return editor;
}

static QWidget* createPropertyEditor(PropertyEnumeration* prop, QWidget* parent)
{
    auto editor = new QComboBox(parent);
    const Enumeration& enumDef = prop->enumeration();
    for (const Enumeration::Mapping& mapping : enumDef.mappings())
        editor->addItem(mapping.string, mapping.value);
    editor->setCurrentIndex(editor->findData(prop->value()));
    auto signalActivated =
            static_cast<void (QComboBox::*)(int)>(&QComboBox::activated);
    QObject::connect(editor, signalActivated, [=](int index) {
        prop->setValue(editor->itemData(index).toInt());
    });
    return editor;
}

static QWidget* createPropertyEditor(PropertyOccColor* prop, QWidget* parent)
{
    auto frame = createPanelEditor(parent);

    auto labelColor = new QLabel(frame);
    const QColor inputColor = occ::QtUtils::toQColor(prop->value());
    labelColor->setPixmap(colorSquarePixmap(inputColor));

    auto labelRgb = new QLabel(frame);
    labelRgb->setText(propertyValueText(prop));

    auto btnColor = new QToolButton(frame);
    btnColor->setText("...");
    btnColor->setToolTip(WidgetPropertiesEditor::tr("Choose color ..."));
    QObject::connect(btnColor, &QAbstractButton::clicked, [=]{
        auto dlg = new QColorDialog(frame);
        dlg->setCurrentColor(inputColor);
        QObject::connect(dlg, &QColorDialog::colorSelected, [=](const QColor& c) {
            prop->setValue(occ::QtUtils::toOccColor(c));
            labelColor->setPixmap(colorSquarePixmap(c));
        });
        qtgui::QWidgetUtils::asyncDialogExec(dlg);
    });

    frame->layout()->addWidget(labelColor);
    frame->layout()->addWidget(labelRgb);
    frame->layout()->addWidget(btnColor);
    frame->layout()->addWidget(hSpacerWidget(frame));

    return frame;
}

static QDoubleSpinBox* createOccPntCoordEditor(
        QWidget* parent,
        PropertyOccPnt* prop,
        double (gp_Pnt::*funcGetCoord)() const,
        void (gp_Pnt::*funcSetCoord)(double))
{
    auto editor = new QDoubleSpinBox(parent);
    const double coord = ((prop->value()).*funcGetCoord)();
    const UnitSystem::TranslateResult trRes =
            Options::instance()->unitSystemTranslate(coord * Quantity_Millimeter);
    //editor->setSuffix(QString::fromUtf8(trRes.strUnit));
    editor->setDecimals(Options::instance()->unitSystemDecimals());
    editor->setButtonSymbols(QDoubleSpinBox::NoButtons);
    editor->setRange(std::numeric_limits<double>::min(),
                     std::numeric_limits<double>::max());
    editor->setValue(trRes.value);
    QSizePolicy sp = editor->sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    editor->setSizePolicy(sp);
    editor->setMinimumWidth(25);
    auto signalValueChanged =
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged);
    QObject::connect(editor, signalValueChanged, [=](double value) {
        const double f = trRes.factor;
        value = qFuzzyCompare(f, 1.) ? value : value * f;
        gp_Pnt pnt = prop->value();
        (pnt.*funcSetCoord)(value);
        prop->setValue(pnt);
    });
    return editor;
}

static QWidget* createPropertyEditor(PropertyOccPnt* prop, QWidget* parent)
{
    auto frame = createPanelEditor(parent);
    QLayout* frameLayout = frame->layout();
    frameLayout->addWidget(new QLabel("X", frame));
    frameLayout->addWidget(createOccPntCoordEditor(frame, prop, &gp_Pnt::X, &gp_Pnt::SetX));
    frameLayout->addWidget(new QLabel("Y", frame));
    frameLayout->addWidget(createOccPntCoordEditor(frame, prop, &gp_Pnt::Y, &gp_Pnt::SetY));
    frameLayout->addWidget(new QLabel("Z", frame));
    frameLayout->addWidget(createOccPntCoordEditor(frame, prop, &gp_Pnt::Z, &gp_Pnt::SetZ));
    return frame;
}

class PropertyItemDelegate : public QStyledItemDelegate {
public:
    PropertyItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    double rowHeightFactor() const { return m_rowHeightFactor; }
    void setRowHeightFactor(double v) { m_rowHeightFactor = v; }

    using UnitTranslation = WidgetPropertiesEditor::UnitTranslation;
    bool overridePropertyUnitTranslation(
            const BasePropertyQuantity* prop, UnitTranslation unitTr)
    {
        if (!prop || prop->quantityUnit() != unitTr.unit)
            return false;
        m_mapPropUnitTr.emplace(prop, unitTr);
        return true;
    }

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        if (index.column() == 1) {
            auto prop = qvariant_cast<Property*>(index.data());
            if (prop && prop->dynTypeName() == PropertyOccColor::TypeName) {
                auto propColor = static_cast<PropertyOccColor*>(prop);
                painter->save();

                QApplication::style()->drawPrimitive(
                            QStyle::PE_PanelItemViewItem,
                            &option,
                            painter,
                            option.widget);

                const QColor color = occ::QtUtils::toQColor(propColor->value());
                const QPixmap pixColor = colorSquarePixmap(color, option.rect.height());
                painter->drawPixmap(option.rect.x(), option.rect.y(), pixColor);
                const QString strColor = propertyValueText(propColor);

                QRect labelRect = option.rect;
                labelRect.setX(option.rect.x() + pixColor.width() + 6);
                QApplication::style()->drawItemText(
                            painter,
                            labelRect,
                            Qt::AlignLeft | Qt::AlignVCenter,
                            option.palette,
                            option.state.testFlag(QStyle::State_Enabled),
                            strColor);

                painter->restore();
                return;
            }
        }
        QStyledItemDelegate::paint(painter, option, index);
    }

    QString displayText(const QVariant& value, const QLocale&) const override
    {
        if (value.type() == QVariant::String)
            return value.toString();
        else if (value.canConvert<Property*>()) {
            const auto prop = qvariant_cast<Property*>(value);
            //return propertyValueText(prop);
            const char* propTypeName = prop ? prop->dynTypeName() : "";
            if (propTypeName == PropertyBool::TypeName)
                return propertyValueText(static_cast<const PropertyBool*>(prop));
            if (propTypeName == PropertyInt::TypeName)
                return propertyValueText(static_cast<const PropertyInt*>(prop));
            if (propTypeName == PropertyDouble::TypeName)
                return propertyValueText(static_cast<const PropertyDouble*>(prop));
            if (propTypeName == PropertyQByteArray::TypeName)
                return propertyValueText(static_cast<const PropertyQByteArray*>(prop));
            if (propTypeName == PropertyQString::TypeName)
                return propertyValueText(static_cast<const PropertyQString*>(prop));
            if (propTypeName == PropertyQDateTime::TypeName)
                return propertyValueText(static_cast<const PropertyQDateTime*>(prop));
            if (propTypeName == PropertyOccColor::TypeName)
                return propertyValueText(static_cast<const PropertyOccColor*>(prop));
            if (propTypeName == PropertyOccPnt::TypeName)
                return propertyValueText(static_cast<const PropertyOccPnt*>(prop));
            if (propTypeName == PropertyOccTrsf::TypeName)
                return propertyValueText(static_cast<const PropertyOccTrsf*>(prop));
            if (propTypeName == PropertyEnumeration::TypeName)
                return propertyValueText(static_cast<const PropertyEnumeration*>(prop));
            if (propTypeName == BasePropertyQuantity::TypeName) {
                auto qtyProp = static_cast<const BasePropertyQuantity*>(prop);
                auto itFound = m_mapPropUnitTr.find(qtyProp);
                if (itFound != m_mapPropUnitTr.cend())
                    return propertyValueText(qtyProp, itFound->second);
                else
                    return propertyValueText(qtyProp);
            }
            return WidgetPropertiesEditor::tr("ERROR no stringifier for property type '%1'")
                    .arg(propTypeName);
        }
        return QString();
    }

    QWidget* createEditor(
            QWidget* parent,
            const QStyleOptionViewItem&,
            const QModelIndex& index) const override
    {
        if (index.column() == 0)
            return nullptr;
        auto prop = qvariant_cast<Property*>(index.data());
        if (!prop || prop->isUserReadOnly())
            return nullptr;
        const char* propTypeName = prop->dynTypeName();
        if (propTypeName == PropertyBool::TypeName)
            return createPropertyEditor(static_cast<PropertyBool*>(prop), parent);
        if (propTypeName == PropertyInt::TypeName)
            return createPropertyEditor(static_cast<PropertyInt*>(prop), parent);
        if (propTypeName == PropertyDouble::TypeName)
            return createPropertyEditor(static_cast<PropertyDouble*>(prop), parent);
        if (propTypeName == PropertyQString::TypeName)
            return createPropertyEditor(static_cast<PropertyQString*>(prop), parent);
        if (propTypeName == PropertyOccColor::TypeName)
            return createPropertyEditor(static_cast<PropertyOccColor*>(prop), parent);
        if (propTypeName == PropertyOccPnt::TypeName)
            return createPropertyEditor(static_cast<PropertyOccPnt*>(prop), parent);
        if (propTypeName == PropertyEnumeration::TypeName)
            return createPropertyEditor(static_cast<PropertyEnumeration*>(prop), parent);
        if (propTypeName == BasePropertyQuantity::TypeName)
            return createPropertyEditor(static_cast<BasePropertyQuantity*>(prop), parent);
        return nullptr;
    }

    void setModelData(
            QWidget*, QAbstractItemModel*, const QModelIndex&) const override
    {
        // Disable default behavior that sets item data(property is changed directly)
    }

    QSize sizeHint(
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override
    {
        const QSize baseSize = QStyledItemDelegate::sizeHint(option, index);
        if (index.data(Qt::SizeHintRole).isNull())
            return QSize(baseSize.width(), m_rowHeightFactor * baseSize.height());
        return baseSize;
    }

private:
    double m_rowHeightFactor = 1.;
    std::unordered_map<const BasePropertyQuantity*, UnitTranslation> m_mapPropUnitTr;
};

} // namespace Internal

WidgetPropertiesEditor::WidgetPropertiesEditor(QWidget *parent)
    : QWidget(parent),
      m_ui(new Ui_WidgetPropertiesEditor)
{
    m_ui->setupUi(this);
    //m_ui->treeWidget_Browser->setUniformRowHeights(true);
    m_ui->treeWidget_Browser->setIndentation(15);
    m_itemDelegate = new Internal::PropertyItemDelegate(m_ui->treeWidget_Browser);
    m_ui->treeWidget_Browser->setItemDelegate(m_itemDelegate);

    QObject::connect(
                Options::instance(), &Options::unitSystemSchemaChanged,
                this, &WidgetPropertiesEditor::refreshAllQtProperties);
    QObject::connect(
                Options::instance(), &Options::unitSystemDecimalsChanged,
                this, &WidgetPropertiesEditor::refreshAllQtProperties);
}

WidgetPropertiesEditor::~WidgetPropertiesEditor()
{
    delete m_ui;
}

void WidgetPropertiesEditor::editProperties(PropertyOwner* propertyOwner)
{
    this->releaseObjects();
    if (propertyOwner) {
        m_ui->stack_Browser->setCurrentWidget(m_ui->page_BrowserDetails);
        m_currentPropertyOwner = propertyOwner;
        auto docItem = dynamic_cast<DocumentItem*>(propertyOwner);
        if (docItem) {
            const GuiDocument* guiDoc =
                    GuiApplication::instance()->findGuiDocument(docItem->document());
            if (guiDoc)
                m_currentGpxPropertyOwner = guiDoc->findItemGpx(docItem);
        }
        this->refreshAllQtProperties();
    }
    else {
        m_ui->stack_Browser->setCurrentWidget(m_ui->page_BrowserEmpty);
    }
}

void WidgetPropertiesEditor::editProperties(Span<HandleProperty> spanHndProp)
{
    this->releaseObjects();
    for (HandleProperty& hndProp : spanHndProp)
        m_currentVecHndProperty.push_back(std::move(hndProp));
    if (!m_currentVecHndProperty.empty()) {
        m_ui->stack_Browser->setCurrentWidget(m_ui->page_BrowserDetails);
        this->refreshAllQtProperties();
    }
    else {
        m_ui->stack_Browser->setCurrentWidget(m_ui->page_BrowserEmpty);
    }
}

void WidgetPropertiesEditor::clear()
{
    this->releaseObjects();
    m_ui->stack_Browser->setCurrentWidget(m_ui->page_BrowserEmpty);
}

void WidgetPropertiesEditor::setPropertyEnabled(const Property* prop, bool on)
{
    for (QTreeWidgetItemIterator it(m_ui->treeWidget_Browser); *it; ++it) {
        QTreeWidgetItem* treeItem = *it;
        const QVariant value = treeItem->data(1, Qt::DisplayRole);
        if (value.canConvert<Property*>()
                && qvariant_cast<Property*>(value) == prop)
        {
            Qt::ItemFlags itemFlags = Qt::ItemIsSelectable | Qt::ItemIsEditable;
            if (on)
                itemFlags |= Qt::ItemIsEnabled;
            treeItem->setFlags(itemFlags);
            break;
        }
    }
}

void WidgetPropertiesEditor::addLineWidget(QWidget* widget)
{
    widget->setAutoFillBackground(true);
    auto treeItem = new QTreeWidgetItem(m_ui->treeWidget_Browser);
    treeItem->setFlags(Qt::ItemIsEnabled);
    m_ui->treeWidget_Browser->setFirstItemColumnSpanned(treeItem, true);
    m_ui->treeWidget_Browser->setItemWidget(treeItem, 0, widget);
}

double WidgetPropertiesEditor::rowHeightFactor() const
{
    return m_itemDelegate->rowHeightFactor();
}

void WidgetPropertiesEditor::setRowHeightFactor(double v)
{
    m_itemDelegate->setRowHeightFactor(v);
}

void WidgetPropertiesEditor::createQtProperties(
        const std::vector<Property*>& properties, QTreeWidgetItem* parentItem)
{
    for (Property* prop : properties)
        this->createQtProperty(prop, parentItem);
}

bool WidgetPropertiesEditor::overridePropertyUnitTranslation(
        const BasePropertyQuantity* prop, UnitTranslation unitTr)
{
    return m_itemDelegate->overridePropertyUnitTranslation(prop, unitTr);
}

void WidgetPropertiesEditor::createQtProperty(
        Property* property, QTreeWidgetItem* parentItem)
{
    auto itemProp = new QTreeWidgetItem;
    itemProp->setText(0, property->label());
    itemProp->setData(1, Qt::DisplayRole, QVariant::fromValue<Property*>(property));
    itemProp->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    if (parentItem != nullptr)
        parentItem->addChild(itemProp);
    else
        m_ui->treeWidget_Browser->addTopLevelItem(itemProp);
}

void WidgetPropertiesEditor::refreshAllQtProperties()
{
    m_ui->treeWidget_Browser->clear();

    if (m_currentPropertyOwner && !m_currentGpxPropertyOwner)
        this->createQtProperties(m_currentPropertyOwner->properties(), nullptr);

    if (m_currentPropertyOwner && m_currentGpxPropertyOwner) {
        auto itemGroupData = new QTreeWidgetItem;
        itemGroupData->setText(0, tr("Data"));
        this->createQtProperties(m_currentPropertyOwner->properties(), itemGroupData);
        m_ui->treeWidget_Browser->addTopLevelItem(itemGroupData);
        itemGroupData->setExpanded(true);

        auto itemGroupGpx = new QTreeWidgetItem;
        itemGroupGpx->setText(0, tr("Graphics"));
        this->createQtProperties(m_currentGpxPropertyOwner->properties(), itemGroupGpx);
        m_ui->treeWidget_Browser->addTopLevelItem(itemGroupGpx);
        itemGroupGpx->setExpanded(true);
    }

    // "On-the-fly" properties
    if (!m_currentVecHndProperty.empty()) {
        for (const HandleProperty& propHnd : m_currentVecHndProperty)
            this->createQtProperty(propHnd.get(), nullptr);
    }

    m_ui->treeWidget_Browser->resizeColumnToContents(0);
    m_ui->treeWidget_Browser->resizeColumnToContents(1);
}

void WidgetPropertiesEditor::releaseObjects()
{
    m_currentPropertyOwner = nullptr;
    m_currentGpxPropertyOwner = nullptr;
    m_currentVecHndProperty.clear();
}

} // namespace Mayo
