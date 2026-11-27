Module.preRun = Module.preRun || [];
Module.preRun.push(() => {
    addRunDependency("persistent-storage");
    FS.mkdir("/persist");
    FS.mount(IDBFS, {}, "/persist");
    FS.syncfs(true, error => {
        if (error) {
            abort("Could not load browser save data: " + error);
            return;
        }
        removeRunDependency("persistent-storage");
    });
});

let syncing = false;
let pending = false;
let callbacks = [];
let saveFailed = false;
Module.persistChanges = callback => {
    if (callback) callbacks.push(callback);
    if (syncing) {
        pending = true;
        return;
    }
    syncing = true;
    const completed = callbacks;
    callbacks = [];
    FS.syncfs(false, error => {
        syncing = false;
        if (error) {
            saveFailed = true;
            console.error("Could not save browser data:", error);
            Module.showNotice("Could not save game data in this browser.");
        } else if (saveFailed) {
            saveFailed = false;
            Module.showNotice("");
        }
        for (const callback of completed) callback(!error);
        if (pending) {
            pending = false;
            Module.persistChanges();
        }
    });
};
