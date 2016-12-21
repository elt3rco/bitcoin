// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/optionsmodel.h>

#include <qt/bitcoinunits.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>

#include <interfaces/node.h>
#include <chainparams.h>
#include <policy/policy.h>
#include <validation.h>
#include <net.h>
#include <net_processing.h>  // for maxorphantx
#include <netbase.h>
#include <outputtype.h>
#include <txdb.h> // for -dbcache defaults
#include <qt/intro.h>
#include <util/moneystr.h> // for FormatMoney
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif

#include <QNetworkProxy>
#include <QSettings>
#include <QStringList>

const char *DEFAULT_GUI_PROXY_HOST = "127.0.0.1";

static const QString GetDefaultProxyAddress();

static QString CanonicalMempoolReplacement()
{
    if (!fEnableReplacement) {
        return "never";
    } else if (fReplacementHonourOptOut) {
        return "fee,optin";
    } else {
        return "fee,-optin";
    }
}

OptionsModel::OptionsModel(interfaces::Node& node, QObject *parent, bool resetSettings) :
    QAbstractListModel(parent), m_node(node)
{
    Init(resetSettings);
}

void OptionsModel::addOverriddenOption(const std::string &option)
{
    strOverriddenByCommandLine += QString::fromStdString(option) + "=" + QString::fromStdString(gArgs.GetArg(option, "")) + " ";
}

// Writes all missing QSettings with their default values
void OptionsModel::Init(bool resetSettings)
{
    if (resetSettings)
        Reset();

    checkAndMigrate();

    QSettings settings;

    // Ensure restart flag is unset on client startup
    setRestartRequired(false);

    // These are Qt-only settings:

    // Window
    if (!settings.contains("fHideTrayIcon"))
        settings.setValue("fHideTrayIcon", false);
    fHideTrayIcon = settings.value("fHideTrayIcon").toBool();
    Q_EMIT hideTrayIconChanged(fHideTrayIcon);

    if (!settings.contains("fMinimizeToTray"))
        settings.setValue("fMinimizeToTray", false);
    fMinimizeToTray = settings.value("fMinimizeToTray").toBool() && !fHideTrayIcon;

    if (!settings.contains("fMinimizeOnClose"))
        settings.setValue("fMinimizeOnClose", false);
    fMinimizeOnClose = settings.value("fMinimizeOnClose").toBool();

    // Display
    if (!settings.contains("nDisplayUnit"))
        settings.setValue("nDisplayUnit", BitcoinUnits::BTC);
    nDisplayUnit = BitcoinUnits::fromSetting(settings.value("nDisplayUnit").toString());

    if (!settings.contains("bDisplayAddresses"))
        settings.setValue("bDisplayAddresses", false);
    bDisplayAddresses = settings.value("bDisplayAddresses", false).toBool();

    if (!settings.contains("strThirdPartyTxUrls"))
        settings.setValue("strThirdPartyTxUrls", "");
    strThirdPartyTxUrls = settings.value("strThirdPartyTxUrls", "").toString();

    if (!settings.contains("fCoinControlFeatures"))
        settings.setValue("fCoinControlFeatures", false);
    fCoinControlFeatures = settings.value("fCoinControlFeatures", false).toBool();

    // These are shared with the core or have a command-line parameter
    // and we want command-line parameters to overwrite the GUI settings.
    //
    // If setting doesn't exist create it with defaults.
    //
    // If gArgs.SoftSetArg() or gArgs.SoftSetBoolArg() return false we were overridden
    // by command-line and show this in the UI.

    // Main
    if (Intro::c_just_set_pruning) {
        // Set QSettings for Core
        qlonglong prune_val = gArgs.GetArg("-prune", 0);
        settings.setValue("bPrune", (prune_val > 1));
        if (prune_val > 1) {
            settings.setValue("nPruneSize", ((prune_val * 1024 * 1024) + GB_BYTES - 1) / GB_BYTES);
        }
    }
    if (!gArgs.IsArgSet("-prune")) {
        if (settings.contains("bPrune")) {
            if (settings.value("bPrune").toBool()) {
                if (!settings.contains("nPruneSize"))
                    settings.setValue("nPruneSize", 2);
                // Convert prune size from GB to MiB:
                const uint64_t nPruneSizeMiB = (settings.value("nPruneSize").toInt() * GB_BYTES) >> 20;
                gArgs.ForceSetArg("-prune", nPruneSizeMiB);
            } else {
                gArgs.ForceSetArg("-prune", "0");
            }
        }
    }

    if (!settings.contains("nDatabaseCache"))
        settings.setValue("nDatabaseCache", (qint64)nDefaultDbCache);
    if (!m_node.softSetArg("-dbcache", settings.value("nDatabaseCache").toString().toStdString()))
        addOverriddenOption("-dbcache");

    if (!settings.contains("nThreadsScriptVerif"))
        settings.setValue("nThreadsScriptVerif", DEFAULT_SCRIPTCHECK_THREADS);
    if (!m_node.softSetArg("-par", settings.value("nThreadsScriptVerif").toString().toStdString()))
        addOverriddenOption("-par");

    if (!settings.contains("strDataDir"))
        settings.setValue("strDataDir", Intro::getDefaultDataDirectory());

    // Wallet
#ifdef ENABLE_WALLET
    if (!settings.contains("bSpendZeroConfChange"))
        settings.setValue("bSpendZeroConfChange", true);
    if (!m_node.softSetBoolArg("-spendzeroconfchange", settings.value("bSpendZeroConfChange").toBool()))
        addOverriddenOption("-spendzeroconfchange");
#endif

    // Network
    if (!settings.contains("nNetworkPort"))
        settings.setValue("nNetworkPort", (quint16)Params().GetDefaultPort());
    if (!gArgs.SoftSetArg("-port", settings.value("nNetworkPort").toString().toStdString()))
        addOverriddenOption("-port");

    if (!settings.contains("fUseUPnP"))
        settings.setValue("fUseUPnP", DEFAULT_UPNP);
    if (!m_node.softSetBoolArg("-upnp", settings.value("fUseUPnP").toBool()))
        addOverriddenOption("-upnp");

    if (!settings.contains("fListen"))
        settings.setValue("fListen", DEFAULT_LISTEN);
    if (!m_node.softSetBoolArg("-listen", settings.value("fListen").toBool()))
        addOverriddenOption("-listen");

    if (!settings.contains("fUseProxy"))
        settings.setValue("fUseProxy", false);
    if (!settings.contains("addrProxy"))
        settings.setValue("addrProxy", GetDefaultProxyAddress());
    // Only try to set -proxy, if user has enabled fUseProxy
    if (settings.value("fUseProxy").toBool() && !m_node.softSetArg("-proxy", settings.value("addrProxy").toString().toStdString()))
        addOverriddenOption("-proxy");
    else if(!settings.value("fUseProxy").toBool() && !gArgs.GetArg("-proxy", "").empty())
        addOverriddenOption("-proxy");

    if (!settings.contains("fUseSeparateProxyTor"))
        settings.setValue("fUseSeparateProxyTor", false);
    if (!settings.contains("addrSeparateProxyTor"))
        settings.setValue("addrSeparateProxyTor", GetDefaultProxyAddress());
    // Only try to set -onion, if user has enabled fUseSeparateProxyTor
    if (settings.value("fUseSeparateProxyTor").toBool() && !m_node.softSetArg("-onion", settings.value("addrSeparateProxyTor").toString().toStdString()))
        addOverriddenOption("-onion");
    else if(!settings.value("fUseSeparateProxyTor").toBool() && !gArgs.GetArg("-onion", "").empty())
        addOverriddenOption("-onion");

    // rwconf settings that require a restart
    f_peerbloomfilters = gArgs.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS);

    // Display
    if (!settings.contains("language"))
        settings.setValue("language", "");
    if (!m_node.softSetArg("-lang", settings.value("language").toString().toStdString()))
        addOverriddenOption("-lang");

    language = settings.value("language").toString();
}

/** Helper function to copy contents from one QSettings to another.
 * By using allKeys this also covers nested settings in a hierarchy.
 */
static void CopySettings(QSettings& dst, const QSettings& src)
{
    for (const QString& key : src.allKeys()) {
        dst.setValue(key, src.value(key));
    }
}

/** Back up a QSettings to an ini-formatted file. */
static void BackupSettings(const fs::path& filename, const QSettings& src)
{
    qWarning() << "Backing up GUI settings to" << GUIUtil::boostPathToQString(filename);
    QSettings dst(GUIUtil::boostPathToQString(filename), QSettings::IniFormat);
    dst.clear();
    CopySettings(dst, src);
}

void OptionsModel::Reset()
{
    QSettings settings;

    // Backup old settings to chain-specific datadir for troubleshooting
    BackupSettings(GetDataDir(true) / "guisettings.ini.bak", settings);

    // Save the strDataDir setting
    QString dataDir = Intro::getDefaultDataDirectory();
    dataDir = settings.value("strDataDir", dataDir).toString();

    // Remove rw config file
    gArgs.EraseRWConfigFile();

    // Remove all entries from our QSettings object
    settings.clear();

    // Set strDataDir
    settings.setValue("strDataDir", dataDir);

    // Set prune option iff it was configured in rwconf
    if (gArgs.RWConfigHasPruneOption()) {
        gArgs.ModifyRWConfigFile("prune", gArgs.GetArg("prune", ""));
    }

    // Set that this was reset
    settings.setValue("fReset", true);

    // default setting for OptionsModel::StartAtStartup - disabled
    if (GUIUtil::GetStartOnSystemStartup())
        GUIUtil::SetStartOnSystemStartup(false);
}

int OptionsModel::rowCount(const QModelIndex & parent) const
{
    return OptionIDRowCount;
}

struct ProxySetting {
    bool is_set;
    QString ip;
    QString port;
};

static ProxySetting GetProxySetting(QSettings &settings, const QString &name)
{
    static const ProxySetting default_val = {false, DEFAULT_GUI_PROXY_HOST, QString("%1").arg(DEFAULT_GUI_PROXY_PORT)};
    // Handle the case that the setting is not set at all
    if (!settings.contains(name)) {
        return default_val;
    }
    // contains IP at index 0 and port at index 1
    QStringList ip_port = settings.value(name).toString().split(":", QString::SkipEmptyParts);
    if (ip_port.size() == 2) {
        return {true, ip_port.at(0), ip_port.at(1)};
    } else { // Invalid: return default
        return default_val;
    }
}

static void SetProxySetting(QSettings &settings, const QString &name, const ProxySetting &ip_port)
{
    settings.setValue(name, ip_port.ip + ":" + ip_port.port);
}

static const QString GetDefaultProxyAddress()
{
    return QString("%1:%2").arg(DEFAULT_GUI_PROXY_HOST).arg(DEFAULT_GUI_PROXY_PORT);
}

// read QSettings values and return them
QVariant OptionsModel::data(const QModelIndex & index, int role) const
{
    if(role == Qt::EditRole)
    {
        QSettings settings;
        switch(index.row())
        {
        case StartAtStartup:
            return GUIUtil::GetStartOnSystemStartup();
        case HideTrayIcon:
            return fHideTrayIcon;
        case MinimizeToTray:
            return fMinimizeToTray;
        case NetworkPort:
            return settings.value("nNetworkPort");
        case MapPortUPnP:
#ifdef USE_UPNP
            return settings.value("fUseUPnP");
#else
            return false;
#endif
        case MinimizeOnClose:
            return fMinimizeOnClose;

        // default proxy
        case ProxyUse:
            return settings.value("fUseProxy", false);
        case ProxyIP:
            return GetProxySetting(settings, "addrProxy").ip;
        case ProxyPort:
            return GetProxySetting(settings, "addrProxy").port;

        // separate Tor proxy
        case ProxyUseTor:
            return settings.value("fUseSeparateProxyTor", false);
        case ProxyIPTor:
            return GetProxySetting(settings, "addrSeparateProxyTor").ip;
        case ProxyPortTor:
            return GetProxySetting(settings, "addrSeparateProxyTor").port;

#ifdef ENABLE_WALLET
        case SpendZeroConfChange:
            return settings.value("bSpendZeroConfChange");
        case addresstype:
        {
            OutputType default_address_type;
            if (!ParseOutputType(gArgs.GetArg("-addresstype", ""), default_address_type)) {
                default_address_type = DEFAULT_ADDRESS_TYPE;
            }
            return QString::fromStdString(FormatOutputType(default_address_type));
        }
#endif
        case DisplayUnit:
            return nDisplayUnit;
        case DisplayAddresses:
            return bDisplayAddresses;
        case ThirdPartyTxUrls:
            return strThirdPartyTxUrls;
        case Language:
            return settings.value("language");
        case CoinControlFeatures:
            return fCoinControlFeatures;
        case PruneMiB:
            return qlonglong(gArgs.GetArg("-prune", 0));
        case DatabaseCache:
            return settings.value("nDatabaseCache");
        case ThreadsScriptVerif:
            return settings.value("nThreadsScriptVerif");
        case Listen:
            return settings.value("fListen");
        case maxuploadtarget:
            return qlonglong(g_connman->GetMaxOutboundTarget() / 1024 / 1024);
        case peerbloomfilters:
            return f_peerbloomfilters;
        case mempoolreplacement:
            return CanonicalMempoolReplacement();
        case maxorphantx:
            return qlonglong(gArgs.GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
        case maxmempool:
            return qlonglong(gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE));
        case mempoolexpiry:
            return qlonglong(gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY));
        case rejectunknownscripts:
            return fRequireStandard;
        case minrelaytxfee:
            return qlonglong(::minRelayTxFee.GetFeePerK());
        case bytespersigop:
            return nBytesPerSigOp;
        case bytespersigopstrict:
            return nBytesPerSigOpStrict;
        case limitancestorcount:
            return qlonglong(gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT));
        case limitancestorsize:
            return qlonglong(gArgs.GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT));
        case limitdescendantcount:
            return qlonglong(gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT));
        case limitdescendantsize:
            return qlonglong(gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT));
        case rejectbaremultisig:
            return !fIsBareMultisigStd;
        case datacarriersize:
            return fAcceptDatacarrier ? qlonglong(nMaxDatacarrierBytes) : qlonglong(0);
        case blockmaxsize:
            return qlonglong(gArgs.GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE) / 1000);
        case blockprioritysize:
            return qlonglong(gArgs.GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE) / 1000);
        case blockmaxweight:
            return qlonglong(gArgs.GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT) / 1000);
        default:
            return QVariant();
        }
    }
    return QVariant();
}

// write QSettings values
bool OptionsModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
    bool successful = true; /* set to false on parse error */
    if(role == Qt::EditRole)
    {
        QSettings settings;
        switch(index.row())
        {
        case StartAtStartup:
            successful = GUIUtil::SetStartOnSystemStartup(value.toBool());
            break;
        case HideTrayIcon:
            fHideTrayIcon = value.toBool();
            settings.setValue("fHideTrayIcon", fHideTrayIcon);
    		Q_EMIT hideTrayIconChanged(fHideTrayIcon);
            break;
        case MinimizeToTray:
            fMinimizeToTray = value.toBool();
            settings.setValue("fMinimizeToTray", fMinimizeToTray);
            break;
        case NetworkPort:
            if (settings.value("nNetworkPort") != value) {
                // If the port input box is empty, set to default port
                if (value.toString().isEmpty()) {
                    settings.setValue("nNetworkPort", (quint16)Params().GetDefaultPort());
                }
                else {
                    settings.setValue("nNetworkPort", (quint16)value.toInt());
                }
                setRestartRequired(true);
            }
            break;
        case MapPortUPnP: // core option - can be changed on-the-fly
            settings.setValue("fUseUPnP", value.toBool());
            m_node.mapPort(value.toBool());
            break;
        case MinimizeOnClose:
            fMinimizeOnClose = value.toBool();
            settings.setValue("fMinimizeOnClose", fMinimizeOnClose);
            break;

        // default proxy
        case ProxyUse:
            if (settings.value("fUseProxy") != value) {
                settings.setValue("fUseProxy", value.toBool());
                setRestartRequired(true);
            }
            break;
        case ProxyIP: {
            auto ip_port = GetProxySetting(settings, "addrProxy");
            if (!ip_port.is_set || ip_port.ip != value.toString()) {
                ip_port.ip = value.toString();
                SetProxySetting(settings, "addrProxy", ip_port);
                setRestartRequired(true);
            }
        }
        break;
        case ProxyPort: {
            auto ip_port = GetProxySetting(settings, "addrProxy");
            if (!ip_port.is_set || ip_port.port != value.toString()) {
                ip_port.port = value.toString();
                SetProxySetting(settings, "addrProxy", ip_port);
                setRestartRequired(true);
            }
        }
        break;

        // separate Tor proxy
        case ProxyUseTor:
            if (settings.value("fUseSeparateProxyTor") != value) {
                settings.setValue("fUseSeparateProxyTor", value.toBool());
                setRestartRequired(true);
            }
            break;
        case ProxyIPTor: {
            auto ip_port = GetProxySetting(settings, "addrSeparateProxyTor");
            if (!ip_port.is_set || ip_port.ip != value.toString()) {
                ip_port.ip = value.toString();
                SetProxySetting(settings, "addrSeparateProxyTor", ip_port);
                setRestartRequired(true);
            }
        }
        break;
        case ProxyPortTor: {
            auto ip_port = GetProxySetting(settings, "addrSeparateProxyTor");
            if (!ip_port.is_set || ip_port.port != value.toString()) {
                ip_port.port = value.toString();
                SetProxySetting(settings, "addrSeparateProxyTor", ip_port);
                setRestartRequired(true);
            }
        }
        break;

#ifdef ENABLE_WALLET
        case SpendZeroConfChange:
            if (settings.value("bSpendZeroConfChange") != value) {
                settings.setValue("bSpendZeroConfChange", value);
                setRestartRequired(true);
            }
            break;
        case addresstype:
        {
            const std::string newvalue_str = value.toString().toStdString();
            OutputType oldvalue, newvalue;
            if (!ParseOutputType(gArgs.GetArg("-addresstype", ""), oldvalue)) {
                oldvalue = DEFAULT_ADDRESS_TYPE;
            }
            if (ParseOutputType(newvalue_str, newvalue) && newvalue != oldvalue) {
                gArgs.ModifyRWConfigFile("addresstype", newvalue_str);
                gArgs.ForceSetArg("-addresstype", newvalue_str);
                for (auto& wallet : GetWallets()) {
                    wallet->m_default_address_type = newvalue;
                }
            }
            break;
        }
#endif
        case DisplayUnit:
            setDisplayUnit(value);
            break;
        case DisplayAddresses:
            bDisplayAddresses = value.toBool();
            settings.setValue("bDisplayAddresses", bDisplayAddresses);
            break;
        case ThirdPartyTxUrls:
            if (strThirdPartyTxUrls != value.toString()) {
                strThirdPartyTxUrls = value.toString();
                settings.setValue("strThirdPartyTxUrls", strThirdPartyTxUrls);
                setRestartRequired(true);
            }
            break;
        case Language:
            if (settings.value("language") != value) {
                settings.setValue("language", value);
                setRestartRequired(true);
            }
            break;
        case CoinControlFeatures:
            fCoinControlFeatures = value.toBool();
            settings.setValue("fCoinControlFeatures", fCoinControlFeatures);
            Q_EMIT coinControlFeaturesChanged(fCoinControlFeatures);
            break;
        case PruneMiB:
        {
            const qlonglong llvalue = value.toLongLong();
            if (gArgs.GetArg("-prune", 0) != llvalue) {
                gArgs.ModifyRWConfigFile("prune", value.toString().toStdString());
                settings.setValue("bPrune", (llvalue > 1));
                if (llvalue > 1) {
                    settings.setValue("nPruneSize", ((llvalue * 1024 * 1024) + GB_BYTES - 1) / GB_BYTES);
                }
                setRestartRequired(true);
            }
            break;
        }
        case DatabaseCache:
            if (settings.value("nDatabaseCache") != value) {
                settings.setValue("nDatabaseCache", value);
                setRestartRequired(true);
            }
            break;
        case ThreadsScriptVerif:
            if (settings.value("nThreadsScriptVerif") != value) {
                settings.setValue("nThreadsScriptVerif", value);
                setRestartRequired(true);
            }
            break;
        case Listen:
            if (settings.value("fListen") != value) {
                settings.setValue("fListen", value);
                setRestartRequired(true);
            }
            break;
        case maxuploadtarget:
        {
            qlonglong nv = value.toLongLong();
            if (g_connman->GetMaxOutboundTarget() / 1024 / 1024 != uint64_t(nv)) {
                gArgs.ModifyRWConfigFile("maxuploadtarget", value.toString().toStdString());
                g_connman->SetMaxOutboundTarget(nv * 1024 * 1024);
            }
            break;
        }
        case peerbloomfilters:
            if (f_peerbloomfilters != value) {
                gArgs.ModifyRWConfigFile("peerbloomfilters", strprintf("%d", value.toBool()));
                f_peerbloomfilters = value.toBool();
                setRestartRequired(true);
            }
            break;
        case mempoolreplacement:
        {
            QString nv = value.toString();
            if (nv != CanonicalMempoolReplacement()) {
                if (nv == "never") {
                    fEnableReplacement = false;
                    fReplacementHonourOptOut = true;
                } else if (nv == "fee,optin") {
                    fEnableReplacement = true;
                    fReplacementHonourOptOut = true;
                } else {  // "fee,-optin"
                    fEnableReplacement = true;
                    fReplacementHonourOptOut = false;
                }
                gArgs.ModifyRWConfigFile("mempoolreplacement", nv.toStdString());
            }
            break;
        }
        case maxorphantx:
        {
            unsigned int nMaxOrphanTx = gArgs.GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS);
            unsigned int nNv = value.toLongLong();
            if (nNv != nMaxOrphanTx) {
                std::string strNv = value.toString().toStdString();
                gArgs.ForceSetArg("-maxorphantx", strNv);
                gArgs.ModifyRWConfigFile("maxorphantx", strNv);
                if (nNv < nMaxOrphanTx) {
                    LOCK(cs_main);
                    unsigned int nEvicted = LimitOrphanTxSize(nNv);
                    if (nEvicted > 0) {
                        LogPrint(BCLog::MEMPOOL, "maxorphantx reduced from %d to %d, removed %u tx\n", nMaxOrphanTx, nNv, nEvicted);
                    }
                }
            }
            break;
        }
        case maxmempool:
        {
            long long nOldValue = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE);
            long long nNv = value.toLongLong();
            if (nNv != nOldValue) {
                std::string strNv = value.toString().toStdString();
                gArgs.ForceSetArg("-maxmempool", strNv);
                gArgs.ModifyRWConfigFile("maxmempool", strNv);
                if (nNv < nOldValue) {
                    LimitMempoolSize();
                }
            }
            break;
        }
        case mempoolexpiry:
        {
            long long nOldValue = gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY);
            long long nNv = value.toLongLong();
            if (nNv != nOldValue) {
                std::string strNv = value.toString().toStdString();
                gArgs.ForceSetArg("-mempoolexpiry", strNv);
                gArgs.ModifyRWConfigFile("mempoolexpiry", strNv);
                if (nNv < nOldValue) {
                    LimitMempoolSize();
                }
            }
            break;
        }
        case rejectunknownscripts:
        {
            const bool fNewValue = value.toBool();
            if (fNewValue != fRequireStandard) {
                fRequireStandard = fNewValue;
                // This option is inverted in the config:
                gArgs.ModifyRWConfigFile("acceptnonstdtxn", strprintf("%d", ! fNewValue));
            }
            break;
        }
        case minrelaytxfee:
        {
            CAmount nNv = value.toLongLong();
            if (nNv != ::minRelayTxFee.GetFeePerK()) {
                gArgs.ModifyRWConfigFile("minrelaytxfee", FormatMoney(nNv));
                ::minRelayTxFee = CFeeRate(nNv);
            }
            break;
        }
        case bytespersigop:
        {
            unsigned int nNv = value.toLongLong();
            if (nNv != nBytesPerSigOp) {
                gArgs.ModifyRWConfigFile("bytespersigop", value.toString().toStdString());
                nBytesPerSigOp = nNv;
            }
            break;
        }
        case bytespersigopstrict:
        {
            unsigned int nNv = value.toLongLong();
            if (nNv != nBytesPerSigOpStrict) {
                gArgs.ModifyRWConfigFile("bytespersigopstrict", value.toString().toStdString());
                nBytesPerSigOpStrict = nNv;
            }
            break;
        }
        case limitancestorcount:
        {
            long long nOldValue = gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
            long long nNv = value.toLongLong();
            if (nNv != nOldValue) {
                std::string strNv = value.toString().toStdString();
                gArgs.ForceSetArg("-limitancestorcount", strNv);
                gArgs.ModifyRWConfigFile("limitancestorcount", strNv);
            }
            break;
        }
        case limitancestorsize:
        {
            long long nOldValue = gArgs.GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT);
            long long nNv = value.toLongLong();
            if (nNv != nOldValue) {
                std::string strNv = value.toString().toStdString();
                gArgs.ForceSetArg("-limitancestorsize", strNv);
                gArgs.ModifyRWConfigFile("limitancestorsize", strNv);
            }
            break;
        }
        case limitdescendantcount:
        {
            long long nOldValue = gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
            long long nNv = value.toLongLong();
            if (nNv != nOldValue) {
                std::string strNv = value.toString().toStdString();
                gArgs.ForceSetArg("-limitdescendantcount", strNv);
                gArgs.ModifyRWConfigFile("limitdescendantcount", strNv);
            }
            break;
        }
        case limitdescendantsize:
        {
            long long nOldValue = gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT);
            long long nNv = value.toLongLong();
            if (nNv != nOldValue) {
                std::string strNv = value.toString().toStdString();
                gArgs.ForceSetArg("-limitdescendantsize", strNv);
                gArgs.ModifyRWConfigFile("limitdescendantsize", strNv);
            }
            break;
        }
        case rejectbaremultisig:
        {
            // The config and internal option is inverted
            const bool fNewValue = ! value.toBool();
            if (fNewValue != fIsBareMultisigStd) {
                fIsBareMultisigStd = fNewValue;
                gArgs.ModifyRWConfigFile("permitbaremultisig", strprintf("%d", fNewValue));
            }
            break;
        }
        case datacarriersize:
        {
            const int nNewSize = value.toInt();
            const bool fNewEn = (nNewSize > 0);
            if (fNewEn && unsigned(nNewSize) != nMaxDatacarrierBytes) {
                gArgs.ModifyRWConfigFile("datacarriersize", value.toString().toStdString());
                nMaxDatacarrierBytes = nNewSize;
            }
            if (fNewEn != fAcceptDatacarrier) {
                gArgs.ModifyRWConfigFile("datacarrier", strprintf("%d", fNewEn));
                fAcceptDatacarrier = fNewEn;
            }
            break;
        }
        case blockmaxsize:
        case blockprioritysize:
        case blockmaxweight:
        {
            const int nNewValue_kB = value.toInt();
            const int nOldValue_kB = data(index, role).toInt();
            if (nNewValue_kB != nOldValue_kB) {
                std::string strNv = strprintf("%d000", nNewValue_kB);
                std::string strKey;
                switch(index.row()) {
                    case blockmaxsize:
                        strKey = "blockmaxsize";
                        break;
                    case blockprioritysize:
                        strKey = "blockprioritysize";
                        break;
                    case blockmaxweight:
                        strKey = "blockmaxweight";
                        break;
                }
                gArgs.ForceSetArg("-" + strKey, strNv);
                gArgs.ModifyRWConfigFile(strKey, strNv);
            }
            break;
        }
        default:
            break;
        }
    }

    Q_EMIT dataChanged(index, index);

    return successful;
}

/** Updates current unit in memory, settings and emits displayUnitChanged(newUnit) signal */
void OptionsModel::setDisplayUnit(const QVariant &value)
{
    if (!value.isNull())
    {
        QSettings settings;
        nDisplayUnit = value.toInt();
        settings.setValue("nDisplayUnit", BitcoinUnits::toSetting(nDisplayUnit));
        Q_EMIT displayUnitChanged(nDisplayUnit);
    }
}

bool OptionsModel::getProxySettings(QNetworkProxy& proxy) const
{
    // Directly query current base proxy, because
    // GUI settings can be overridden with -proxy.
    proxyType curProxy;
    if (m_node.getProxy(NET_IPV4, curProxy)) {
        proxy.setType(QNetworkProxy::Socks5Proxy);
        proxy.setHostName(QString::fromStdString(curProxy.proxy.ToStringIP()));
        proxy.setPort(curProxy.proxy.GetPort());

        return true;
    }
    else
        proxy.setType(QNetworkProxy::NoProxy);

    return false;
}

void OptionsModel::setRestartRequired(bool fRequired)
{
    QSettings settings;
    return settings.setValue("fRestartRequired", fRequired);
}

bool OptionsModel::isRestartRequired() const
{
    QSettings settings;
    return settings.value("fRestartRequired", false).toBool();
}

void OptionsModel::checkAndMigrate()
{
    // Migration of default values
    // Check if the QSettings container was already loaded with this client version
    QSettings settings;
    static const char strSettingsVersionKey[] = "nSettingsVersion";
    int settingsVersion = settings.contains(strSettingsVersionKey) ? settings.value(strSettingsVersionKey).toInt() : 0;
    if (settingsVersion < CLIENT_VERSION)
    {
        // -dbcache was bumped from 100 to 300 in 0.13
        // see https://github.com/bitcoin/bitcoin/pull/8273
        // force people to upgrade to the new value if they are using 100MB
        if (settingsVersion < 130000 && settings.contains("nDatabaseCache") && settings.value("nDatabaseCache").toLongLong() == 100)
            settings.setValue("nDatabaseCache", (qint64)nDefaultDbCache);

        settings.setValue(strSettingsVersionKey, CLIENT_VERSION);
    }

    // Overwrite the 'addrProxy' setting in case it has been set to an illegal
    // default value (see issue #12623; PR #12650).
    if (settings.contains("addrProxy") && settings.value("addrProxy").toString().endsWith("%2")) {
        settings.setValue("addrProxy", GetDefaultProxyAddress());
    }

    // Overwrite the 'addrSeparateProxyTor' setting in case it has been set to an illegal
    // default value (see issue #12623; PR #12650).
    if (settings.contains("addrSeparateProxyTor") && settings.value("addrSeparateProxyTor").toString().endsWith("%2")) {
        settings.setValue("addrSeparateProxyTor", GetDefaultProxyAddress());
    }
}
