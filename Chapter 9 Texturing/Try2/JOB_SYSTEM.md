# JobSystem

Внутренний `enki::TaskScheduler` один на движок. JobSystem занимается только CPU-работой.
`Init(workers)` создаёт workers; `Shutdown()` дожидается задач и дочерних задач и останавливает потоки.

```cpp
auto task = jobs.Dispatch(count, rangeCallback, rangeSize, JobSystem::Priority::High);
jobs.WaitHighPriority(task);
auto background = jobs.Submit(callback, JobSystem::Priority::Low);
jobs.Wait(background);
```

`Submit` и `Dispatch` по умолчанию используют `Normal`; доступны High, Normal, Low.
`Wait(handle, lowestToRun)` позволяет выполнять только задачи с приоритетом не ниже указанного.
`WaitHighPriority` — ожидание с High-фильтром. Если фильтр исключает целевую задачу, Wait бросает
`invalid_argument`, чтобы исключить зависание. Исключения callbacks передаются через Wait/WaitAll.
`WaitAll` и Shutdown работают со всеми приоритетами. Handles сохраняют результат после Shutdown.

`RunHighPriorityTasks()` даёт worker возможность помогать срочной работе между единицами
длительной фоновой задачи. Вытеснения внутри callback нет. При переполнении очереди enkiTS
Submit может исполнять callback на вызывающем потоке, поэтому ResourceManager допускает
лишь несколько consumers и самостоятельно ограничивает их число. JobSystem не знает
о ресурсах, физике, GPU или финализации.

Lifecycle и WaitAll выполняются на потоке Init вне callback. Submit/Dispatch/Wait доступны
на этом же потоке и из его задач; произвольные внешние потоки не зарегистрированы в scheduler.
