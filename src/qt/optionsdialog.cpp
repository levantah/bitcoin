// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/optionsdialog.h>
#include <qt/forms/ui_optionsdialog.h>

#include <qt/bitcoinunits.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>

#include <interfaces/node.h>
#include <validation.h> // for DEFAULT_SCRIPTCHECK_THREADS and MAX_SCRIPTCHECK_THREADS
#include <netbase.h>
#include <txdb.h> // for -dbcache defaults
#include <txmempool.h> // for maxmempoolMinimum

#include <QBoxLayout>
#include <QDataWidgetMapper>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QSpacerItem>
#include <QString>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

void OptionsDialog::FixTabOrder(QWidget * const o)
{
    setTabOrder(prevwidget, o);
    prevwidget = o;
}

void OptionsDialog::CreateOptionUI(QBoxLayout * const layout, QWidget * const o, const QString& text)
{
    QWidget * const parent = o->parentWidget();
    const QStringList text_parts = text.split("%s");

    QHBoxLayout * const horizontalLayout = new QHBoxLayout();

    QLabel * const labelBefore = new QLabel(parent);
    labelBefore->setText(text_parts[0]);
    labelBefore->setTextFormat(Qt::PlainText);
    labelBefore->setBuddy(o);
    labelBefore->setToolTip(o->toolTip());

    horizontalLayout->addWidget(labelBefore);
    horizontalLayout->addWidget(o);

    QLabel * const labelAfter = new QLabel(parent);
    labelAfter->setText(text_parts[1]);
    labelAfter->setTextFormat(Qt::PlainText);
    labelAfter->setBuddy(o);
    labelAfter->setToolTip(o->toolTip());

    horizontalLayout->addWidget(labelAfter);

    QSpacerItem * const horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    horizontalLayout->addItem(horizontalSpacer);

    layout->addLayout(horizontalLayout);

    FixTabOrder(o);
}

OptionsDialog::OptionsDialog(QWidget *parent, bool enableWallet) :
    QDialog(parent),
    ui(new Ui::OptionsDialog),
    model(nullptr),
    mapper(nullptr)
{
    ui->setupUi(this);

    /* Main elements init */
    ui->databaseCache->setMinimum(nMinDbCache);
    ui->databaseCache->setMaximum(nMaxDbCache);
    ui->threadsScriptVerif->setMinimum(-GetNumCores());
    ui->threadsScriptVerif->setMaximum(MAX_SCRIPTCHECK_THREADS);
    ui->pruneWarning->setVisible(false);
    ui->pruneWarning->setStyleSheet("QLabel { color: red; }");

    ui->pruneSize->setEnabled(false);
    connect(ui->prune, &QPushButton::toggled, ui->pruneSize, &QWidget::setEnabled);
    connect(ui->prune, &QPushButton::toggled, [this](bool nv) {
        ui->peercfilters->setEnabled(!nv);
    });
    connect(ui->peercfilters, &QPushButton::toggled, [this](bool nv) {
        ui->prune->setEnabled(!nv);
    });

    ui->networkPort->setValidator(new QIntValidator(1024, 65535, this));
    connect(ui->networkPort, SIGNAL(textChanged(const QString&)), this, SLOT(checkLineEdit()));

    /* Network elements init */
#ifndef USE_UPNP
    ui->mapPortUpnp->setEnabled(false);
#endif

    ui->proxyIp->setEnabled(false);
    ui->proxyPort->setEnabled(false);
    ui->proxyPort->setValidator(new QIntValidator(1, 65535, this));

    ui->proxyIpTor->setEnabled(false);
    ui->proxyPortTor->setEnabled(false);
    ui->proxyPortTor->setValidator(new QIntValidator(1, 65535, this));

    connect(ui->connectSocks, &QPushButton::toggled, ui->proxyIp, &QWidget::setEnabled);
    connect(ui->connectSocks, &QPushButton::toggled, ui->proxyPort, &QWidget::setEnabled);
    connect(ui->connectSocks, &QPushButton::toggled, this, &OptionsDialog::updateProxyValidationState);

    connect(ui->connectSocksTor, &QPushButton::toggled, ui->proxyIpTor, &QWidget::setEnabled);
    connect(ui->connectSocksTor, &QPushButton::toggled, ui->proxyPortTor, &QWidget::setEnabled);
    connect(ui->connectSocksTor, &QPushButton::toggled, this, &OptionsDialog::updateProxyValidationState);

    ui->maxuploadtarget->setMinimum(144 /* MB/day */);
    ui->maxuploadtarget->setMaximum(std::numeric_limits<int>::max());
    connect(ui->maxuploadtargetCheckbox, SIGNAL(toggled(bool)), ui->maxuploadtarget, SLOT(setEnabled(bool)));

    prevwidget = ui->peerbloomfilters;

    /* Mempool tab */

    QWidget * const tabMempool = new QWidget();
    QVBoxLayout * const verticalLayout_Mempool = new QVBoxLayout(tabMempool);
    ui->tabWidget->insertTab(ui->tabWidget->indexOf(ui->tabWindow), tabMempool, tr("Mem&pool"));

    mempoolreplacement = new QValueComboBox(tabMempool);
    mempoolreplacement->addItem(QString("never"), QVariant("never"));
    mempoolreplacement->addItem(QString("with a higher mining fee, and opt-in"), QVariant("fee,optin"));
    mempoolreplacement->addItem(QString("with a higher mining fee (no opt-out)"), QVariant("fee,-optin"));
    CreateOptionUI(verticalLayout_Mempool, mempoolreplacement, tr("Transaction &replacement: %s"));

    maxorphantx = new QSpinBox(tabMempool);
    maxorphantx->setMinimum(0);
    maxorphantx->setMaximum(std::numeric_limits<int>::max());
    CreateOptionUI(verticalLayout_Mempool, maxorphantx, tr("Keep at most %s unconnected transactions in memory"));

    maxmempool = new QSpinBox(tabMempool);
    const int64_t nMempoolSizeMinMB = maxmempoolMinimum(gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT));
    maxmempool->setMinimum(nMempoolSizeMinMB);
    maxmempool->setMaximum(std::numeric_limits<int>::max());
    CreateOptionUI(verticalLayout_Mempool, maxmempool, tr("Keep the transaction memory pool below %s MB"));

    mempoolexpiry = new QSpinBox(tabMempool);
    mempoolexpiry->setMinimum(1);
    mempoolexpiry->setMaximum(std::numeric_limits<int>::max());
    CreateOptionUI(verticalLayout_Mempool, mempoolexpiry, tr("Do not keep transactions in memory more than %s hours"));

    QGroupBox * const groupBox_Spamfiltering = new QGroupBox(tabMempool);
    groupBox_Spamfiltering->setTitle(tr("Spam filtering"));
    QVBoxLayout * const verticalLayout_Spamfiltering = new QVBoxLayout(groupBox_Spamfiltering);

    rejectunknownscripts = new QCheckBox(groupBox_Spamfiltering);
    rejectunknownscripts->setText(tr("Ignore unrecognised receiver scripts"));
    rejectunknownscripts->setToolTip(tr("With this option enabled, unrecognised receiver (\"pubkey\") scripts will be ignored. Unrecognisable scripts could be used to bypass further spam filters. If your software is outdated, they may also be used to trick you into thinking you were sent bitcoins that will never confirm."));
    verticalLayout_Spamfiltering->addWidget(rejectunknownscripts);
    FixTabOrder(rejectunknownscripts);

    bytespersigop = new QSpinBox(groupBox_Spamfiltering);
    bytespersigop->setMinimum(1);
    bytespersigop->setMaximum(std::numeric_limits<int>::max());
    CreateOptionUI(verticalLayout_Spamfiltering, bytespersigop, tr("Treat each consensus-counted sigop as at least %s bytes."));

    bytespersigopstrict = new QSpinBox(groupBox_Spamfiltering);
    bytespersigopstrict->setMinimum(1);
    bytespersigopstrict->setMaximum(std::numeric_limits<int>::max());
    CreateOptionUI(verticalLayout_Spamfiltering, bytespersigopstrict, tr("Ignore transactions with fewer than %s bytes per potentially-executed sigop."));

    limitancestorcount = new QSpinBox(groupBox_Spamfiltering);
    limitancestorcount->setMinimum(1);
    limitancestorcount->setMaximum(std::numeric_limits<int>::max());
    CreateOptionUI(verticalLayout_Spamfiltering, limitancestorcount, tr("Ignore transactions with %s or more unconfirmed ancestors."));

    limitancestorsize = new QSpinBox(groupBox_Spamfiltering);
    limitancestorsize->setMinimum(1);
    limitancestorsize->setMaximum(std::numeric_limits<int>::max());
    CreateOptionUI(verticalLayout_Spamfiltering, limitancestorsize, tr("Ignore transactions whose size with all unconfirmed ancestors exceeds %s kilobytes."));

    verticalLayout_Mempool->addWidget(groupBox_Spamfiltering);

    verticalLayout_Mempool->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    /* Window elements init */
#ifdef Q_OS_MAC
    /* remove Window tab on Mac */
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabWindow));
#if  defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED > 101100
    /* hide launch at startup option if compiled against macOS > 10.11 (removed API) */
    ui->bitcoinAtStartup->setVisible(false);
    ui->verticalLayout_Main->removeWidget(ui->bitcoinAtStartup);
    ui->verticalLayout_Main->removeItem(ui->horizontalSpacer_0_Main);
#endif
#endif

    /* remove Wallet tab in case of -disablewallet */
    if (!enableWallet) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabWallet));
    }

    /* Display elements init */
    QDir translations(":translations");

    ui->bitcoinAtStartup->setToolTip(ui->bitcoinAtStartup->toolTip().arg(PACKAGE_NAME));
    ui->bitcoinAtStartup->setText(ui->bitcoinAtStartup->text().arg(PACKAGE_NAME));

    ui->openBitcoinConfButton->setToolTip(ui->openBitcoinConfButton->toolTip().arg(PACKAGE_NAME));

    ui->lang->setToolTip(ui->lang->toolTip().arg(PACKAGE_NAME));
    ui->lang->addItem(QString("(") + tr("default") + QString(")"), QVariant(""));
    for (const QString &langStr : translations.entryList())
    {
        QLocale locale(langStr);

        /** check if the locale name consists of 2 parts (language_country) */
        if(langStr.contains("_"))
        {
            /** display language strings as "native language - native country (locale name)", e.g. "Deutsch - Deutschland (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" - ") + locale.nativeCountryName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
        }
        else
        {
            /** display language strings as "native language (locale name)", e.g. "Deutsch (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
        }
    }
    ui->thirdPartyTxUrls->setPlaceholderText("https://example.com/tx/%s");

    ui->unit->setModel(new BitcoinUnits(this));

    /* Widget-to-option mapper */
    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
    mapper->setOrientation(Qt::Vertical);

    GUIUtil::ItemDelegate* delegate = new GUIUtil::ItemDelegate(mapper);
    connect(delegate, &GUIUtil::ItemDelegate::keyEscapePressed, this, &OptionsDialog::reject);
    mapper->setItemDelegate(delegate);

    /* setup/change UI elements when proxy IPs are invalid/valid */
    ui->proxyIp->setCheckValidator(new ProxyAddressValidator(parent));
    ui->proxyIpTor->setCheckValidator(new ProxyAddressValidator(parent));
    connect(ui->proxyIp, &QValidatedLineEdit::validationDidChange, this, &OptionsDialog::updateProxyValidationState);
    connect(ui->proxyIpTor, &QValidatedLineEdit::validationDidChange, this, &OptionsDialog::updateProxyValidationState);
    connect(ui->proxyPort, &QLineEdit::textChanged, this, &OptionsDialog::updateProxyValidationState);
    connect(ui->proxyPortTor, &QLineEdit::textChanged, this, &OptionsDialog::updateProxyValidationState);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        ui->hideTrayIcon->setChecked(true);
        ui->hideTrayIcon->setEnabled(false);
        ui->minimizeToTray->setChecked(false);
        ui->minimizeToTray->setEnabled(false);
    }

    GUIUtil::handleCloseWindowShortcut(this);
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::setModel(OptionsModel *_model)
{
    this->model = _model;

    if(_model)
    {
        /* check if client restart is needed and show persistent message */
        if (_model->isRestartRequired())
            showRestartWarning(true);

        // Prune values are in GB to be consistent with intro.cpp
        static constexpr uint64_t MiB_BYTES = 1024 * 1024;
        static constexpr uint64_t nMinDiskSpace = (MIN_DISK_SPACE_FOR_BLOCK_FILES + MiB_BYTES - 1) / MiB_BYTES;
        ui->pruneSize->setRange(nMinDiskSpace, std::numeric_limits<int>::max());

        QString strLabel = _model->getOverriddenByCommandLine();
        if (strLabel.isEmpty())
            strLabel = tr("none");
        ui->overriddenByCommandLineLabel->setText(strLabel);

        mapper->setModel(_model);
        setMapper();
        mapper->toFirst();

        updateDefaultProxyNets();
    }

    /* warn when one of the following settings changes by user action (placed here so init via mapper doesn't trigger them) */

    /* Main */
    connect(ui->prune, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->prune, &QCheckBox::clicked, this, &OptionsDialog::togglePruneWarning);
    connect(ui->pruneSize, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &OptionsDialog::showRestartWarning);
    connect(ui->databaseCache, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &OptionsDialog::showRestartWarning);
    connect(ui->threadsScriptVerif, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &OptionsDialog::showRestartWarning);
    /* Wallet */
    connect(ui->spendZeroConfChange, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    /* Network */
    connect(ui->networkPort, SIGNAL(textChanged(const QString &)), this, SLOT(showRestartWarning()));
    connect(ui->allowIncoming, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->connectSocks, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->connectSocksTor, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->peerbloomfilters, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    connect(ui->peercfilters, &QCheckBox::clicked, this, &OptionsDialog::showRestartWarning);
    /* Display */
    connect(ui->lang, static_cast<void (QValueComboBox::*)()>(&QValueComboBox::valueChanged), [this]{ showRestartWarning(); });
    connect(ui->thirdPartyTxUrls, &QLineEdit::textChanged, [this]{ showRestartWarning(); });
}

void OptionsDialog::setCurrentTab(OptionsDialog::Tab tab)
{
    QWidget *tab_widget = nullptr;
    if (tab == OptionsDialog::Tab::TAB_NETWORK) tab_widget = ui->tabNetwork;
    if (tab == OptionsDialog::Tab::TAB_MAIN) tab_widget = ui->tabMain;
    if (tab_widget && ui->tabWidget->currentWidget() != tab_widget) {
        ui->tabWidget->setCurrentWidget(tab_widget);
    }
}

void OptionsDialog::setMapper()
{
    /* Main */
    mapper->addMapping(ui->bitcoinAtStartup, OptionsModel::StartAtStartup);
    mapper->addMapping(ui->threadsScriptVerif, OptionsModel::ThreadsScriptVerif);
    mapper->addMapping(ui->databaseCache, OptionsModel::DatabaseCache);

    qlonglong current_prune = model->data(model->index(OptionsModel::PruneMiB, 0), Qt::EditRole).toLongLong();
    if (current_prune == 0) {
        ui->prune->setChecked(false);
        ui->pruneSize->setEnabled(false);
    } else if (current_prune == 1) {
        // Manual pruning
        ui->prune->setTristate();
        ui->prune->setCheckState(Qt::PartiallyChecked);
        ui->pruneSize->setEnabled(false);
    } else {
        ui->prune->setChecked(true);
        ui->pruneSize->setEnabled(true);
        ui->pruneSize->setValue(current_prune);
    }

    /* Wallet */
    mapper->addMapping(ui->spendZeroConfChange, OptionsModel::SpendZeroConfChange);
    mapper->addMapping(ui->coinControlFeatures, OptionsModel::CoinControlFeatures);

    {
        QString radio_name_lower = "addresstype" + model->data(model->index(OptionsModel::addresstype, 0), Qt::EditRole).toString().toLower();
        radio_name_lower.replace("-", "_");
        for (int i = ui->layoutAddressType->count(); i--; ) {
            QRadioButton * const radio = qobject_cast<QRadioButton*>(ui->layoutAddressType->itemAt(i)->widget());
            if (!radio) {
                continue;
            }
            radio->setChecked(radio->objectName().toLower() == radio_name_lower);
        }
    }

    /* Network */
    mapper->addMapping(ui->networkPort, OptionsModel::NetworkPort);
    mapper->addMapping(ui->mapPortUpnp, OptionsModel::MapPortUPnP);
    mapper->addMapping(ui->allowIncoming, OptionsModel::Listen);

    mapper->addMapping(ui->connectSocks, OptionsModel::ProxyUse);
    mapper->addMapping(ui->proxyIp, OptionsModel::ProxyIP);
    mapper->addMapping(ui->proxyPort, OptionsModel::ProxyPort);

    mapper->addMapping(ui->connectSocksTor, OptionsModel::ProxyUseTor);
    mapper->addMapping(ui->proxyIpTor, OptionsModel::ProxyIPTor);
    mapper->addMapping(ui->proxyPortTor, OptionsModel::ProxyPortTor);

    int current_maxuploadtarget = model->data(model->index(OptionsModel::maxuploadtarget, 0), Qt::EditRole).toInt();
    if (current_maxuploadtarget == 0) {
        ui->maxuploadtargetCheckbox->setChecked(false);
        ui->maxuploadtarget->setEnabled(false);
        ui->maxuploadtarget->setValue(ui->maxuploadtarget->minimum());
    } else {
        if (current_maxuploadtarget < ui->maxuploadtarget->minimum()) {
            ui->maxuploadtarget->setMinimum(current_maxuploadtarget);
        }
        ui->maxuploadtargetCheckbox->setChecked(true);
        ui->maxuploadtarget->setEnabled(true);
        ui->maxuploadtarget->setValue(current_maxuploadtarget);
    }

    mapper->addMapping(ui->peerbloomfilters, OptionsModel::peerbloomfilters);
    mapper->addMapping(ui->peercfilters, OptionsModel::peercfilters);

    /* Mempool tab */

    QVariant current_mempoolreplacement = model->data(model->index(OptionsModel::mempoolreplacement, 0), Qt::EditRole);
    int current_mempoolreplacement_index = mempoolreplacement->findData(current_mempoolreplacement);
    if (current_mempoolreplacement_index == -1) {
        mempoolreplacement->addItem(current_mempoolreplacement.toString(), current_mempoolreplacement);
        current_mempoolreplacement_index = mempoolreplacement->count() - 1;
    }
    mempoolreplacement->setCurrentIndex(current_mempoolreplacement_index);

    mapper->addMapping(maxorphantx, OptionsModel::maxorphantx);
    mapper->addMapping(maxmempool, OptionsModel::maxmempool);
    mapper->addMapping(mempoolexpiry, OptionsModel::mempoolexpiry);

    mapper->addMapping(rejectunknownscripts, OptionsModel::rejectunknownscripts);
    mapper->addMapping(bytespersigop, OptionsModel::bytespersigop);
    mapper->addMapping(bytespersigopstrict, OptionsModel::bytespersigopstrict);
    mapper->addMapping(limitancestorcount, OptionsModel::limitancestorcount);
    mapper->addMapping(limitancestorsize, OptionsModel::limitancestorsize);

    /* Window */
#ifndef Q_OS_MAC
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        mapper->addMapping(ui->hideTrayIcon, OptionsModel::HideTrayIcon);
        mapper->addMapping(ui->minimizeToTray, OptionsModel::MinimizeToTray);
    }
    mapper->addMapping(ui->minimizeOnClose, OptionsModel::MinimizeOnClose);
#endif

    /* Display */
    mapper->addMapping(ui->lang, OptionsModel::Language);
    mapper->addMapping(ui->unit, OptionsModel::DisplayUnit);
    mapper->addMapping(ui->displayAddresses, OptionsModel::DisplayAddresses);
    mapper->addMapping(ui->thirdPartyTxUrls, OptionsModel::ThirdPartyTxUrls);
}

void OptionsDialog::checkLineEdit()
{
    QLineEdit * const lineedit = qobject_cast<QLineEdit*>(QObject::sender());
    if (lineedit->hasAcceptableInput()) {
        lineedit->setStyleSheet("");
    } else {
        lineedit->setStyleSheet("color: red;");
    }
}

void OptionsDialog::setOkButtonState(bool fState)
{
    ui->okButton->setEnabled(fState);
}

void OptionsDialog::on_resetButton_clicked()
{
    if(model)
    {
        // confirmation dialog
        QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm options reset"),
            tr("Client restart required to activate changes.") + "<br><br>" + tr("Client will be shut down. Do you want to proceed?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

        if(btnRetVal == QMessageBox::Cancel)
            return;

        /* reset all options and close GUI */
        model->Reset();
        QApplication::quit();
    }
}

void OptionsDialog::on_openBitcoinConfButton_clicked()
{
    /* explain the purpose of the config file */
    QMessageBox::information(this, tr("Configuration options"),
        tr("The configuration file is used to specify advanced user options which override GUI settings. "
           "Additionally, any command-line options will override this configuration file."));

    /* show an error if there was some problem opening the file */
    if (!GUIUtil::openBitcoinConf())
        QMessageBox::critical(this, tr("Error"), tr("The configuration file could not be opened."));
}

void OptionsDialog::on_okButton_clicked()
{
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        QWidget * const tab = ui->tabWidget->widget(i);
        Q_FOREACH(QObject* o, tab->children()) {
            QLineEdit * const lineedit = qobject_cast<QLineEdit*>(o);
            if (lineedit && !lineedit->hasAcceptableInput()) {
                int row = mapper->mappedSection(lineedit);
                if (model->data(model->index(row, 0), Qt::EditRole) == lineedit->text()) {
                    // Allow unchanged fields through
                    continue;
                }
                ui->tabWidget->setCurrentWidget(tab);
                lineedit->setFocus(Qt::OtherFocusReason);
                lineedit->selectAll();
                QMessageBox::critical(this, tr("Invalid setting"), tr("The value entered is invalid."));
                return;
            }
        }
    }

    switch (ui->prune->checkState()) {
    case Qt::Unchecked:
        model->setData(model->index(OptionsModel::PruneMiB, 0), 0);
        break;
    case Qt::PartiallyChecked:
        model->setData(model->index(OptionsModel::PruneMiB, 0), 1);
        break;
    case Qt::Checked:
        model->setData(model->index(OptionsModel::PruneMiB, 0), ui->pruneSize->value());
        break;
    }

    {
        QString new_addresstype;
        for (int i = ui->layoutAddressType->count(); i--; ) {
            QRadioButton * const radio = qobject_cast<QRadioButton*>(ui->layoutAddressType->itemAt(i)->widget());
            if (!(radio && radio->objectName().startsWith("addressType") && radio->isChecked())) {
                continue;
            }
            new_addresstype = radio->objectName().mid(11).toLower();
            new_addresstype.replace("_", "-");
            break;
        }
        model->setData(model->index(OptionsModel::addresstype, 0), new_addresstype);
    }

    if (ui->maxuploadtargetCheckbox->isChecked()) {
        model->setData(model->index(OptionsModel::maxuploadtarget, 0), ui->maxuploadtarget->value());
    } else {
        model->setData(model->index(OptionsModel::maxuploadtarget, 0), 0);
    }

    model->setData(model->index(OptionsModel::mempoolreplacement, 0), mempoolreplacement->itemData(mempoolreplacement->currentIndex()));

    mapper->submit();
    accept();
    updateDefaultProxyNets();
}

void OptionsDialog::on_cancelButton_clicked()
{
    reject();
}

void OptionsDialog::on_hideTrayIcon_stateChanged(int fState)
{
    if(fState)
    {
        ui->minimizeToTray->setChecked(false);
        ui->minimizeToTray->setEnabled(false);
    }
    else
    {
        ui->minimizeToTray->setEnabled(true);
    }
}

void OptionsDialog::togglePruneWarning(bool enabled)
{
    ui->pruneWarning->setVisible(!ui->pruneWarning->isVisible());
}

void OptionsDialog::showRestartWarning(bool fPersistent)
{
    ui->statusLabel->setStyleSheet("QLabel { color: red; }");

    if(fPersistent)
    {
        ui->statusLabel->setText(tr("Client restart required to activate changes."));
    }
    else
    {
        ui->statusLabel->setText(tr("This change would require a client restart."));
        // clear non-persistent status label after 10 seconds
        // Todo: should perhaps be a class attribute, if we extend the use of statusLabel
        QTimer::singleShot(10000, this, &OptionsDialog::clearStatusLabel);
    }
}

void OptionsDialog::clearStatusLabel()
{
    ui->statusLabel->clear();
    if (model && model->isRestartRequired()) {
        showRestartWarning(true);
    }
}

void OptionsDialog::updateProxyValidationState()
{
    QValidatedLineEdit *pUiProxyIp = ui->proxyIp;
    QValidatedLineEdit *otherProxyWidget = (pUiProxyIp == ui->proxyIpTor) ? ui->proxyIp : ui->proxyIpTor;
    if (pUiProxyIp->isValid() && (!ui->proxyPort->isEnabled() || ui->proxyPort->text().toInt() > 0) && (!ui->proxyPortTor->isEnabled() || ui->proxyPortTor->text().toInt() > 0))
    {
        setOkButtonState(otherProxyWidget->isValid()); //only enable ok button if both proxys are valid
        clearStatusLabel();
    }
    else
    {
        setOkButtonState(false);
        ui->statusLabel->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel->setText(tr("The supplied proxy address is invalid."));
    }
}

void OptionsDialog::updateDefaultProxyNets()
{
    proxyType proxy;
    std::string strProxy;
    QString strDefaultProxyGUI;

    model->node().getProxy(NET_IPV4, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachIPv4->setChecked(true) : ui->proxyReachIPv4->setChecked(false);

    model->node().getProxy(NET_IPV6, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachIPv6->setChecked(true) : ui->proxyReachIPv6->setChecked(false);

    model->node().getProxy(NET_ONION, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachTor->setChecked(true) : ui->proxyReachTor->setChecked(false);
}

ProxyAddressValidator::ProxyAddressValidator(QObject *parent) :
QValidator(parent)
{
}

QValidator::State ProxyAddressValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);
    // Validate the proxy
    CService serv(LookupNumeric(input.toStdString().c_str(), DEFAULT_GUI_PROXY_PORT));
    proxyType addrProxy = proxyType(serv, true);
    if (addrProxy.IsValid())
        return QValidator::Acceptable;

    return QValidator::Invalid;
}
