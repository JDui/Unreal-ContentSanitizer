#pragma once

#include "CoreMinimal.h"
#include "Model/SanitizerOrphanTypes.h"

struct FSanitizerOrphanScanSession;

class CONTENTSANITIZERCORE_API FSanitizerOrphanScanService
{
public:
    FSanitizerOrphanScanService();
    ~FSanitizerOrphanScanService();

    void BeginScan(const FSanitizerOrphanScanRequest& Request);
    bool TickScan(double TimeBudgetSeconds = 0.008, int32 MaxAssetsPerTick = 64);
    void RequestCancel();
    bool IsScanActive() const;

    const FSanitizerOrphanScanProgress& GetProgress() const;
    const FSanitizerOrphanScanResult& GetResult() const;

    FSanitizerOrphanScanResult Scan(const FSanitizerOrphanScanRequest& Request);

private:
    TAtomic<bool> bCancelRequested { false };
    uint64 NextRevision = 1;
    TUniquePtr<FSanitizerOrphanScanSession> Session;
};
