#pragma once
#include "Commons.h"
#include <TaskScheduler.h>

class JobSystem
{
public:
    static JobSystem& Get();

    void Init();
    void Shutdown();

    void Submit(/* job */);

private:
    enki::TaskScheduler scheduler;

};

