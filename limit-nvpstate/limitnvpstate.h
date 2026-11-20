#pragma once

#include <ui_limitnvpstate.h>
#include <vector>
#include <rules.h>

class QTimer;

class limitnvpstate : public QMainWindow {
    Q_OBJECT

public:
    limitnvpstate(QWidget* parent = nullptr);

private:
    Ui::limitnvpstateClass ui;

    void setupProcessRunningTrigger();
    void stopProcessRunningTrigger();
    void setupProcessForegroundTrigger();
    void stopProcessForegroundTrigger();
    void createTrayIcon();
    void saveProcessRules();
    void refreshRuleList();
    void reevaluatePState();
    void getAvailablePStates();

    // process-specific P-State overrides (same order as the list widget)
    std::vector<ProcessRule> processRules;
    // P-States of the selected GPU that can be used as a limit (excludes P0)
    std::vector<int> availablePStates;

    // periodic re-check while the foreground trigger is active
    QTimer* foregroundTimer = nullptr;

private slots:
    void unlimitTriggerChanged(int index);
    void selectedGPUChanged(int index);
    void selectedPStateChanged(int index);
    void addProcess();
    void editProcess();
    void exitApp(int exitCode);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* e) override;
};

