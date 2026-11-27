#!/usr/bin/env node
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { access, mkdir, mkdtemp, readFile, rm, stat, writeFile } from "node:fs/promises";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)),"..");
const usage = `Usage: node tools/capture-effects.mjs [options]
  --group pulselaser             Gun definition key, hullshield, or sphereshield
  --effect muzzle                muzzle, projectile (with trail), impact,
                                 shield-idle, shield-hit, shield-break, shield-recharge
  --fps 60 --frames 90            Fixed simulation rate and exact screenshot count
  --width 640 --height 640        Capture size in pixels
  --out <new-directory>          Output directory (never overwrites a capture)
  --set group.field=value        Repeatable effect/global configuration override
  --settings <settings.ini>      Load an explicitly saved configuration first
  --build <directory>            Existing browser build (default: build/web)
  --browser <executable>         Chrome/Chromium, or set CHROME_PATH
  --help                         Show this help
`;

function options(args) {
    const result = {group:"pulselaser",effect:"muzzle",fps:60,frames:90,width:640,height:640,
        build:path.join(root,"build/web"),out:path.join(root,"build/effect-captures",
            new Date().toISOString().replaceAll(":","-")),set:[]};
    for (let i=0;i<args.length;i++) {
        const key = args[i].slice(2);
        if (!args[i].startsWith("--") || !["group","effect","fps","frames","width","height",
            "out","set","settings","build","browser"].includes(key))
            throw Error(`Unknown option: ${args[i]}\n${usage}`);
        const value = args[++i];
        if (!value || value.startsWith("--")) throw Error(`Missing value for --${key}`);
        if (key === "set") result.set.push(value); else result[key] = value;
    }
    for (const [key,min,max] of [["fps",1,240],["frames",1,14401],["width",256,2048],["height",256,2048]]) {
        const value = Number(result[key]);
        if (!Number.isInteger(value) || value<min || value>max) throw Error(`--${key} must be ${min}..${max}`);
        result[key] = value;
    }
    result.build = path.resolve(result.build); result.out = path.resolve(result.out);
    return result;
}

async function browserPath(explicit) {
    const candidates = explicit ? [explicit] : [
        process.env.CHROME_PATH,
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/usr/bin/google-chrome","/usr/bin/chromium","/usr/bin/chromium-browser",
        process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES,"Google/Chrome/Application/chrome.exe"),
        process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA,"Google/Chrome/Application/chrome.exe")
    ].filter(Boolean);
    for (const candidate of candidates) {
        try { await access(candidate); return candidate; }
        catch (error) { if (error.code !== "ENOENT") throw error; }
    }
    throw Error("Chrome/Chromium not found. Use --browser or CHROME_PATH.");
}

async function capture(config) {
    if (typeof WebSocket !== "function") throw Error("This tool requires Node.js 22 or newer.");
    await access(path.join(config.build,"voxellancer-web.html"));
    const executable = await browserPath(config.browser);
    await mkdir(path.dirname(config.out),{recursive:true});
    await mkdir(config.out);
    const profile = await mkdtemp(path.join(os.tmpdir(),"voxellancer-effect-lab-"));
    const logs = [];
    let browser, socket, server;
    try {
        server = http.createServer(async (request,response) => {
            try {
                const relative = decodeURIComponent(new URL(request.url,"http://localhost").pathname).slice(1);
                const file = path.resolve(config.build,relative);
                if (!file.startsWith(config.build+path.sep)) {
                    response.writeHead(403).end(); return;
                }
                const info = await stat(file);
                if (!info.isFile()) { response.writeHead(404).end(); return; }
                const mime = {".html":"text/html",".js":"text/javascript",".wasm":"application/wasm",
                    ".data":"application/octet-stream"}[path.extname(file)] || "application/octet-stream";
                response.writeHead(200,{"Content-Type":mime,"Content-Length":info.size});
                if (request.method === "HEAD") response.end();
                else createReadStream(file).on("error",error => response.destroy(error)).pipe(response);
            } catch (error) {
                if (error.code === "ENOENT") response.writeHead(404).end();
                else { logs.push(String(error)); response.writeHead(500).end(); }
            }
        });
        await new Promise((resolve,reject) => {
            server.once("error",reject); server.listen(0,"127.0.0.1",resolve);
        });
        const port = server.address().port;
        const ready = await fetch(`http://127.0.0.1:${port}/voxellancer-web.html`,{method:"HEAD"});
        if (!ready.ok) throw Error("Local capture server is not responsive.");
        browser = spawn(executable,["--headless","--disable-background-networking","--disable-extensions",
            "--disable-sync","--use-gl=angle","--use-angle=swiftshader",
            "--enable-unsafe-swiftshader","--no-first-run",`--user-data-dir=${profile}`,
            "--remote-debugging-port=0",`--window-size=${config.width},${config.height}`,"about:blank"],
            {stdio:["ignore","ignore","pipe"]});
        const endpoint = await new Promise((resolve,reject) => {
            const timer = setTimeout(()=>reject(Error("Chrome did not start DevTools within 20 seconds.")),20000);
            let output = "";
            browser.once("error",error => { clearTimeout(timer); reject(error); });
            browser.once("exit",code => { clearTimeout(timer); reject(Error(`Chrome exited: ${code}`)); });
            browser.stderr.on("data",chunk => {
                logs.push(chunk.toString()); output += chunk;
                const match = /DevTools listening on (ws:\/\/[^\s]+)/.exec(output);
                if (match) { clearTimeout(timer); resolve(new URL(match[1])); }
            });
        });
        const tabs = await (await fetch(`http://${endpoint.host}/json`)).json();
        const tab = tabs.find(entry => entry.type === "page");
        if (!tab) throw Error("Chrome did not create a page.");
        socket = new WebSocket(tab.webSocketDebuggerUrl);
        await new Promise((resolve,reject) => { socket.onopen=resolve; socket.onerror=reject; });
        let next = 0;
        const pending = new Map(), exceptions = [];
        socket.onmessage = ({data}) => {
            const message = JSON.parse(data);
            if (message.id) {
                const callback = pending.get(message.id);
                if (!callback) return;
                pending.delete(message.id); clearTimeout(callback.timer);
                if (message.error) callback.reject(Error(message.error.message));
                else callback.resolve(message.result);
            }
            if (message.method === "Runtime.exceptionThrown") exceptions.push(message.params);
            if (message.method === "Runtime.consoleAPICalled")
                logs.push(JSON.stringify(message.params));
        };
        const send = (method,params={}) => new Promise((resolve,reject) => {
            const id = ++next;
            const timer = setTimeout(()=>{pending.delete(id);reject(Error(`DevTools timed out: ${method}`));},60000);
            pending.set(id,{resolve,reject,timer}); socket.send(JSON.stringify({id,method,params}));
        });
        const evaluate = async expression => {
            const result = await send("Runtime.evaluate",{expression,returnByValue:true,awaitPromise:true});
            if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails));
            return result.result.value;
        };
        await send("Runtime.enable"); await send("Page.enable");
        await send("Emulation.setDeviceMetricsOverride",{width:config.width,height:config.height,
            deviceScaleFactor:1,mobile:false});
        const query = new URLSearchParams({scenario:"effect-lab",group:config.group,
            effect:config.effect,fps:String(config.fps),capture:"1"});
        await send("Page.navigate",{url:`http://127.0.0.1:${port}/voxellancer-web.html?${query}`});
        let started = false;
        for (let i=0;i<600;i++) {
            const state = await evaluate(`({
                ready:typeof Module !== "undefined" && !!Module.effectLab,
                error:typeof Module !== "undefined" && Module.effectLabError,
                status:document.getElementById("status")?.textContent
            })`);
            if (state.error || /could not|failed|stopped/i.test(state.status || ""))
                throw Error(state.error || state.status);
            if (state.ready) { started=true; break; }
            await new Promise(resolve=>setTimeout(resolve,100));
        }
        if (!started) throw Error("Effect lab did not become ready within 60 seconds.");
        if (config.settings) {
            const text = await readFile(config.settings,"utf8");
            await evaluate(`Module.effectLab.loadSettings(${JSON.stringify(text)})`);
        }
        for (const override of config.set) {
            const match = /^(\w+)\.(\w+)=(.+)$/.exec(override);
            if (!match || !Number.isFinite(Number(match[3]))) throw Error(`Invalid --set: ${override}`);
            await evaluate(`Module.effectLab.set(${JSON.stringify(match[1])},${JSON.stringify(match[2])},${Number(match[3])})`);
        }
        const manifest = {version:1,complete:false,width:config.width,height:config.height,
            ...await evaluate("Module.effectLab.describe()"),
            browser:await send("Browser.getVersion"),
            renderer:await evaluate(`(() => {
                const gl = Module.ctx, debug = gl.getExtension("WEBGL_debug_renderer_info");
                return {name:gl.getParameter(debug ? debug.UNMASKED_RENDERER_WEBGL : gl.RENDERER),
                    version:gl.getParameter(gl.VERSION)};
            })()`),
            overrides:config.set,frames:[]};
        const manifestPath = path.join(config.out,"manifest.json");
        await writeFile(manifestPath,JSON.stringify(manifest,null,2));
        for (let index=0;index<config.frames;index++) {
            const metrics = await evaluate(`Module.effectLab.frame(${index})`);
            const screenshot = await send("Page.captureScreenshot",{format:"png",fromSurface:true,
                captureBeyondViewport:false});
            const bytes = Buffer.from(screenshot.data,"base64");
            if (bytes.toString("hex",0,8) !== "89504e470d0a1a0a" ||
                bytes.readUInt32BE(16) !== config.width || bytes.readUInt32BE(20) !== config.height)
                throw Error(`Screenshot ${index} has an unexpected PNG size or format.`);
            const file = `frame-${String(index).padStart(5,"0")}.png`;
            await writeFile(path.join(config.out,file),bytes);
            manifest.frames.push({index,time:index/config.fps,file,...metrics,
                sha256:createHash("sha256").update(bytes).digest("hex")});
        }
        if (exceptions.length) throw Error(JSON.stringify(exceptions));
        const lastVisible = manifest.frames.findLastIndex(frame=>frame.visiblePixels>0);
        const end = lastVisible < 0 ? manifest.frames.length-1 :
            Math.min(manifest.frames.length-1,lastVisible+1);
        const tileCount = Math.min(15,end+1);
        const selected = Array.from({length:tileCount},(_,i) =>
            manifest.frames[Math.round(i*end/Math.max(1,tileCount-1))]);
        if (selected.at(-1) !== manifest.frames.at(-1)) selected.push(manifest.frames.at(-1));
        const tiles = [];
        for (const frame of selected) {
            const data = (await readFile(path.join(config.out,frame.file))).toString("base64");
            tiles.push(`<figure><img src="data:image/png;base64,${data}"><figcaption>`+
                `#${frame.index} / ${frame.time.toFixed(3)}s / ${frame.visiblePixels}px</figcaption></figure>`);
        }
        const thumbHeight = Math.round(240*config.height/config.width);
        const sheetStyle = `body{margin:0;background:#07101a;color:#c7e4f0;font:13px monospace}
            main{display:grid;grid-template-columns:repeat(4,240px)}
            figure{margin:0}img{display:block;width:240px;height:${thumbHeight}px}figcaption{height:24px}`;
        await send("Page.navigate",{url:"about:blank"});
        await send("Emulation.setDeviceMetricsOverride",{width:960,
            height:Math.ceil(selected.length/4)*(thumbHeight+24),deviceScaleFactor:1,mobile:false});
        await evaluate(`document.body.innerHTML=${JSON.stringify(`<style>${sheetStyle}</style><main>${tiles.join("")}</main>`)}`);
        await evaluate("Promise.all(Array.from(document.images,image=>image.decode()))");
        const sheet = await send("Page.captureScreenshot",{format:"png",captureBeyondViewport:false});
        await writeFile(path.join(config.out,"contact-sheet.png"),Buffer.from(sheet.data,"base64"));
        const report = `<!doctype html><meta charset="utf-8"><title>Effect capture</title>
<style>body{background:#07101a;color:#c7e4f0;font:16px monospace}img{max-width:100%}#frame{display:block;width:min(80vw,640px)}</style>
<h1>${config.group} / ${config.effect}</h1><p>${config.fps} FPS / ${config.frames} frames.
Single production effect, fixed camera and time step. No game simulation or audio.</p>
<button id="play">Play / Pause</button><input id="seek" type="range" min="0" max="${config.frames-1}" value="0">
<span id="label"></span><img id="frame" src="${manifest.frames[0].file}">
<h2>Contact sheet</h2><img src="contact-sheet.png"><p><a href="manifest.json">Frame metrics and exact configuration</a></p>
<script>
const frames=${JSON.stringify(manifest.frames)}, fps=${config.fps};
const seek=document.getElementById("seek"), image=document.getElementById("frame"), label=document.getElementById("label");
let playing=false;
function show(){const f=frames[Number(seek.value)];image.src=f.file;label.textContent="#"+f.index+" / "+f.time.toFixed(3)+"s / "+f.visiblePixels+" visible pixels";}
seek.oninput=()=>{playing=false;show();};document.getElementById("play").onclick=()=>playing=!playing;
setInterval(()=>{if(playing){seek.value=(Number(seek.value)+1)%frames.length;show();}},1000/fps);show();
</script>`;
        await writeFile(path.join(config.out,"index.html"),report);
        manifest.complete = true;
        await writeFile(manifestPath,JSON.stringify(manifest,null,2));
        console.log(`Captured ${config.frames} consecutive frames at ${config.fps} FPS: ${config.out}`);
        console.log(`Visible frames: ${manifest.frames.filter(frame=>frame.visiblePixels>0).length}; open index.html or contact-sheet.png.`);
    } catch (error) {
        await writeFile(path.join(config.out,"capture-error.txt"),String(error.stack || error));
        throw error;
    } finally {
        if (socket) socket.close();
        if (browser && browser.exitCode === null) {
            const exited = new Promise(resolve=>browser.once("exit",resolve));
            browser.kill("SIGTERM");
            let timer;
            await Promise.race([exited,new Promise(resolve=>{timer=setTimeout(resolve,5000);})]);
            clearTimeout(timer);
            if (browser.exitCode === null) { browser.kill("SIGKILL"); await exited; }
        }
        if (server) { server.closeAllConnections(); await new Promise(resolve=>server.close(resolve)); }
        await writeFile(path.join(config.out,"capture.log"),logs.join("\n"));
        await rm(profile,{recursive:true,force:true});
    }
}

if (process.argv.includes("--help")) console.log(usage);
else {
    try { await capture(options(process.argv.slice(2))); }
    catch (error) { console.error(`Effect capture failed: ${error.message}`); process.exitCode=1; }
}
