class MQTT {
    initialized = false;
    init() { this.initialized = true; }
    async loadMQTT() {
        return getJSONSync('/mqttsettings', (err, settings) => {
            if (err) 
                console.log(err);
            else {
                console.log(settings);
                ui.toElement(document.getElementById('divMQTT'), { mqtt: settings });
                document.getElementById('divDiscoveryTopic').style.display = settings.pubDisco ? '' : 'none';
            }
        });
    }
    connectMQTT() {
        let obj = ui.fromElement(document.getElementById('divMQTT'));
        console.log(obj);
        if (obj.mqtt.enabled) {
            if (typeof obj.mqtt.hostname !== 'string' || obj.mqtt.hostname.length === 0) {
                ui.errorMessage('Invalid host name').querySelector('.sub-message').innerHTML = 'You must supply a host name to connect to MQTT.';
                return;
            }
            if (obj.mqtt.hostname.length > 64) {
                ui.errorMessage('Invalid host name').querySelector('.sub-message').innerHTML = 'The maximum length of the host name is 64 characters.';
                return;
            }
            if (isNaN(obj.mqtt.port) || obj.mqtt.port < 0) {
                ui.errorMessage('Invalid port number').querySelector('.sub-message').innerHTML = 'Likely ports are 1183, 8883 for MQTT/S or 80,443 for HTTP/S';
                return;
            }
            if (typeof obj.mqtt.username === 'string' && obj.mqtt.username.length > 32) {
                ui.errorMessage('Invalid Username').querySelector('.sub-message').innerHTML = 'The maximum length of the username is 32 characters.';
                return;
            }
            if (typeof obj.mqtt.password === 'string' && obj.mqtt.password.length > 32) {
                ui.errorMessage('Invalid Password').querySelector('.sub-message').innerHTML = 'The maximum length of the password is 32 characters.';
                return;
            }
            if (typeof obj.mqtt.rootTopic === 'string' && obj.mqtt.rootTopic.length > 64) {
                ui.errorMessage('Invalid Root Topic').querySelector('.sub-message').innerHTML = 'The maximum length of the root topic is 64 characters.';
                return;
            }
        }
        putJSONSync('/connectmqtt', obj.mqtt, (err, response) => {
            if (err) ui.serviceError(err);
            console.log(response);
        });
    }
}
var mqtt = new MQTT();
class Firmware {
    initialized = false;
    init() { this.initialized = true; }
    isMobile() {
        let agt = navigator.userAgent.toLowerCase();
        return /Android|iPhone|iPad|iPod|BlackBerry|BB|PlayBook|IEMobile|Windows Phone|Kindle|Silk|Opera Mini/i.test(navigator.userAgent);
    }
    async backup() {
        let overlay = ui.waitMessage(document.getElementById('divContainer'));
        return await new Promise((resolve, reject) => {
            let xhr = new XMLHttpRequest();
            xhr.responseType = 'blob';
            xhr.onreadystatechange = (evt) => {
                if (xhr.readyState === 4 && xhr.status === 200) {
                    let obj = window.URL.createObjectURL(xhr.response);
                    var link = document.createElement('a');
                    document.body.appendChild(link);
                    let header = xhr.getResponseHeader('content-disposition');
                    let fname = 'backup';
                    if (typeof header !== 'undefined') {
                        let start = header.indexOf('filename="');
                        if (start >= 0) {
                            let length = header.length;
                            fname = header.substring(start + 10, length - 1);
                        }
                    }
                    console.log(fname);
                    link.setAttribute('download', fname);
                    link.setAttribute('href', obj);
                    link.click();
                    link.remove();
                    setTimeout(() => { window.URL.revokeObjectURL(obj); console.log('Revoked object'); }, 0);
                }
            };
            xhr.onload = (evt) => {
                if (typeof overlay !== 'undefined') overlay.remove();
                let status = xhr.status;
                if (status !== 200) {
                    let err = xhr.response || {};
                    err.htmlError = status;
                    err.service = `GET /backup`;
                    if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
                    console.log('Done');
                    reject(err);
                }
                else {
                    resolve();
                }
            };
            xhr.onerror = (evt) => {
                if (typeof overlay !== 'undefined') overlay.remove();
                let err = {
                    htmlError: xhr.status || 500,
                    service: `GET /backup`
                };
                if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
                console.log(err);
                reject(err);
            };
            xhr.onabort = (evt) => {
                if (typeof overlay !== 'undefined') overlay.remove();
                console.log('Aborted');
                if (typeof overlay !== 'undefined') overlay.remove();
                reject({ htmlError: status, service: 'GET /backup' });
            };
            xhr.open('GET', baseUrl.length > 0 ? `${baseUrl}/backup` : '/backup', true);
            xhr.send();
        });
    }
    restore() {
        let div = this.createFileUploader('/restore');
        let inst = div.querySelector('div[id=divInstText]');
        let html = '<div style="font-size:14px;">Select a backup file that you would like to restore and the options you would like to restore then press the Upload File button.</div><hr />';
        html += `<div style="font-size:14px;">Restoring network settings from a different board than the original will ignore Ethernet chip settings. Security, MQTT and WiFi connection information will also not be restored since backup files do not contain passwords.</div><hr/>`;
        html += '<div style="font-size:14px;margin-bottom:27px;text-align:left;margin-left:70px;">';
        html += `<div class="field-group" style="vertical-align:middle;width:auto;"><input id="cbRestoreShades" type="checkbox" data-bind="shades" style="display:inline-block;" checked="true" /><label for="cbRestoreShades" style="display:inline-block;cursor:pointer;color:white;">Restore Shades and Groups</label></div>`;
        html += `<div class="field-group" style="vertical-align:middle;width:auto;"><input id="cbRestoreRepeaters" type="checkbox" data-bind="repeaters" style="display:inline-block;" /><label for="cbRestoreRepeaters" style="display:inline-block;cursor:pointer;color:white;">Restore Repeaters</label></div>`;
        html += `<div class="field-group" style="vertical-align:middle;width:auto;"><input id="cbRestoreSystem" type="checkbox" data-bind="settings" style="display:inline-block;" /><label for="cbRestoreSystem" style="display:inline-block;cursor:pointer;color:white;">Restore System Settings</label></div>`;
        html += `<div class="field-group" style="vertical-align:middle;width:auto;"><input id="cbRestoreNetwork" type="checkbox" data-bind="network" style="display:inline-block;" /><label for="cbRestoreNetwork" style="display:inline-block;cursor:pointer;color:white;">Restore Network Settings</label></div>`
        html += `<div class="field-group" style="vertical-align:middle;width:auto;"><input id="cbRestoreMQTT" type="checkbox" data-bind="mqtt" style="display:inline-block;" /><label for="cbRestoreMQTT" style="display:inline-block;cursor:pointer;color:white;">Restore MQTT Settings</label></div>`
        html += `<div class="field-group" style="vertical-align:middle;width:auto;"><input id="cbRestoreTransceiver" type="checkbox" data-bind="transceiver" style="display:inline-block;" /><label for="cbRestoreTransceiver" style="display:inline-block;cursor:pointer;color:white;">Restore Radio Settings</label></div>`;
        html += '</div>';
        inst.innerHTML = html;
        document.getElementById('divContainer').appendChild(div);
    }
    createFileUploader(service) {
        let div = document.createElement('div');
        div.setAttribute('id', 'divUploadFile');
        div.setAttribute('class', 'inst-overlay');
        div.style.width = '100%';
        div.style.alignContent = 'center';
        let html = `<div style="width:100%;text-align:center;"><form method="POST" action="#" enctype="multipart/form-data" id="frmUploadApp" style="">`;
        html += `<div id="divInstText"></div>`;
        html += `<input id="fileName" type="file" name="updateFS" style="display:none;" onchange="document.getElementById('span-selected-file').innerText = this.files[0].name;"/>`;
        html += `<label for="fileName">`;
        html += `<span id="span-selected-file" style="display:inline-block;width:calc(100% - 47px);border-bottom:solid 2px white;font-size:14px;white-space:nowrap;overflow:hidden;max-width:320px;text-overflow:ellipsis;"></span>`;
        html += `<div id="btn-select-file" class="button-outline" style="font-size:.8em;padding:10px;"><i class="icss-upload" style="margin:0px;"></i></div>`;
        html += `</label>`;
        html += `<div class="progress-bar" id="progFileUpload" style="--progress:0%;margin-top:10px;display:none;"></div>`;
        html += `<div class="button-container">`;
        html += `<button id="btnBackupCfg" type="button" style="display:none;width:auto;padding-left:20px;padding-right:20px;margin-right:4px;" onclick="firmware.backup();">Backup</button>`;
        html += `<button id="btnUploadFile" type="button" style="width:auto;padding-left:20px;padding-right:20px;margin-right:4px;display:inline-block;" onclick="firmware.uploadFile('${service}', document.getElementById('divUploadFile'), ui.fromElement(document.getElementById('divUploadFile')));">Upload File</button>`;
        html += `<button id="btnClose" type="button" style="width:auto;padding-left:20px;padding-right:20px;display:inline-block;" onclick="document.getElementById('divUploadFile').remove();">Cancel</button></div>`;
        html += `</form><div>`;
        div.innerHTML = html;
        return div;
    }
    procMemoryStatus(mem) {
        console.log(mem);
        let sp = document.getElementById('spanFreeMemory');
        if (sp) sp.innerHTML = mem.free.fmt("#,##0");
        sp = document.getElementById('spanMaxMemory');
        if (sp) sp.innerHTML = mem.max.fmt('#,##0');
        sp = document.getElementById('spanMinMemory');
        if (sp) sp.innerHTML = mem.min.fmt('#,##0');


    }

    procFwStatus(rel) {
        console.log(rel);
        let div = document.getElementById('divFirmwareUpdate');
        if (rel.available && rel.status === 0 && rel.checkForUpdate !== false) {
            div.style.color = 'black';
            div.innerHTML = `<span>Firmware ${rel.fwVersion.name} Installed<span><span style="color:red"> ${rel.latest.name} Available</span>`;
        }
        else {
            switch (rel.status) {
                case 2: // Awaiting update.
                    div.style.color = 'red';
                    div.innerHTML = `Preparing firmware update`;
                    break;
                case 3: // Updating -- this will be set by the update progress.
                    break;
                case 4: // Complete
                    if (rel.error !== 0) {
                        div.style.color = 'red';
                        let e = errors.find(x => x.code === rel.error) || { code: rel.error, desc: 'Unspecified error' };
                        let inst = document.getElementById('divGitInstall');
                        if (inst) {
                            inst.remove();
                            ui.errorMessage(e.desc);
                        }
                        div.innerHTML = e.desc;
                    }
                    else {
                        div.innerHTML = `Firmware update complete`;
                        // Throw up a wait message this will be cleared on the reload.
                        ui.waitMessage(document.getElementById('divContainer'));
                    }
                    break;
                case 5:
                    div.style.color = 'red';
                    div.innerHTML = `Cancelling firmware update`;
                    break;
                case 6:
                    div.style.color = 'red';
                    div.innerHTML = `Firmware update cancelled`;
                    break;

                default:
                    div.style.color = 'black';
                    div.innerHTML = `Firmware ${rel.fwVersion.name} Installed`;
                    break;
            }
        }
        div.style.display = '';
    }
    procUpdateProgress(prog) {
        let pct = Math.round((prog.loaded / prog.total) * 100);
        let file = prog.part === 100 ? 'Application' : 'Firmware';
        let div = document.getElementById('divFirmwareUpdate');
        if (div) {
            div.style.color = 'red';
            div.innerHTML = `Updating ${file} to ${prog.ver} ${pct}%`;
        }
        general.reloadApp = true;
        let git = document.getElementById('divGitInstall');
        if (git) {
            // Update the status on the client that started the install.
            if (pct >= 100 && prog.part === 100) git.remove();
            else {
                if (prog.part === 100) {
                    document.getElementById('btnCancelUpdate').style.display = 'none';
                }
                let p = prog.part === 100 ? document.getElementById('progApplicationDownload') : document.getElementById('progFirmwareDownload');
                if (p) {
                    p.style.setProperty('--progress', `${pct}%`);
                    p.setAttribute('data-progress', `${pct}%`);
                }
            }
        }

    }
    async installGitRelease(div) {
        if (!this.isMobile()) {
            console.log('Starting backup');
            try {
                await firmware.backup();
                console.log('Backup Complete');
            }
            catch (err) {
                ui.serviceError(el, err);
                return;
            }
        }

        let obj = ui.fromElement(div);
        console.log(obj);
        putJSONSync(`/downloadFirmware?ver=${obj.version}`, {}, (err, ver) => {
            if (err) ui.serviceError(err);
            else {
                general.reloadApp = true;
                // Change the display and allow the percentage to be shown when the socket emits the progress.
                let html = `<div>Installing ${ver.name}</div><div style="font-size:.7em;margin-top:4px;">Please wait as the files are downloaded and installed.  Once the application update process starts you may no longer cancel the update as this will corrupt the downloaded files.</div>`;
                html += `<div class="progress-bar" id="progFirmwareDownload" style="--progress:0%;margin-top:10px;text-align:center;"></div>`;
                html += `<label for="progFirmwareDownload" style="font-size:10pt;">Firmware Install Progress</label>`;
                html += `<div class="progress-bar" id="progApplicationDownload" style="--progress:0%;margin-top:10px;text-align:center;"></div>`;
                html += `<label for="progFirmwareDownload" style="font-size:10pt;">Application Install Progress</label>`;
                html += `<hr></hr><div class="button-container" style="text-align:center;">`;
                html += `<button id="btnCancelUpdate" type="button" style="width:40%;padding-left:20px;padding-right:20px;display:inline-block;" onclick="firmware.cancelInstallGit(document.getElementById('divGitInstall'));">Cancel</button>`;
                html += `</div>`;
                div.innerHTML = html;
            }
        });
    }
    cancelInstallGit(div) {
        putJSONSync(`/cancelFirmware`, {}, (err, ver) => {
            if (err) ui.serviceError(err);
            else console.log(ver);
            div.remove();
        });
    }
    updateGithub() {
        getJSONSync('/getReleases', (err, rel) => {
            if (err) ui.serviceError(err);
            else {
                console.log(rel);
                let div = document.createElement('div');
                let chip = document.getElementById('divContainer').getAttribute('data-chipmodel');
                div.setAttribute('id', 'divGitInstall')
                div.setAttribute('class', 'inst-overlay');
                div.style.width = '100%';
                div.style.alignContent = 'center';
                // Sort the releases so that the pre-releases are at the bottom.
                rel.releases.sort((a, b) => a.preRelease === b.preRelease && b.draft === a.draft ? 0 : a.preRelease ? 1 : -1);

                let html = `<div>Select a version from the repository to install using the dropdown below.  Then press the update button to install that version.</div><div style="font-size:.7em;margin-top:4px;">Select Main to install the most recent alpha version from the repository.</div>`;
                html += `<div id="divPrereleaseWarning" style="display:none;width:100%;color:red;text-align:center;font-weight:bold;"><span style="margin-top:7px;width:100%;padding:3px;display:inline-block;border-radius:5px;background:white;">WARNING<span><hr style="margin:0px" /><div style="font-size:.7em;padding-left:1em;padding-right:1em;color:black;font-weight:normal;">You have selected a pre-released beta version that has not been fully tested or published for general use.</div></div>`;
                html += `<div class="field-group" style="text-align:center;">`;
                html += `<select id="selVersion" data-bind="version" style="width:70%;font-size:2em;color:white;text-align-last:center;" onchange="firmware.gitReleaseSelected(document.getElementById('divGitInstall'));">`
                for (let i = 0; i < rel.releases.length; i++) {
                    if (rel.releases[i].hwVersions.length === 0 || rel.releases[i].hwVersions.indexOf(chip) >= 0)
                        html += `<option style="text-align:left;font-size:.5em;color:black;" data-prerelease="${rel.releases[i].preRelease}" value="${rel.releases[i].version.name}">${rel.releases[i].name}${rel.releases[i].preRelease ? ' - Pre' : ''}</option>`
                }
                html += `</select><label for="selVersion">Select a version</label></div>`;
                html += `<div class="button-container" id="divReleaseNotes" style="text-align:center;margin-top:-20px;display:none;"><button type="button" onclick="firmware.showReleaseNotes(document.getElementById('selVersion').value);" style="display:inline-block;width:auto;padding-left:20px;padding-right:20px;">Release Notes</button></div>`;
                if (this.isMobile()) {
                    html += `<div style="width:100%;color:red;text-align:center;font-weight:bold;"><span style="margin-top:7px;width:100%;background:yellow;padding:3px;display:inline-block;border-radius:5px;background:white;">WARNING<span></div>`;
                    html += '<hr/><div style="font-size:14px;margin-bottom:10px;">This browser does not support automatic backups.  It is highly recommended that you back up your configuration using the backup button before proceeding.</div>';
                }
                else {
                    html += '<hr/><div style="font-size:14px;margin-bottom:10px;">A backup file for your configuration will be downloaded to your browser.  If the firmware update process fails please restore this file using the restore button after going through the onboarding process.</div>'
                }
                html += `<hr></hr><div class="button-container" style="text-align:center;">`;
                if (this.isMobile()) {
                    html += `<button id="btnBackupCfg" type="button" style="display:inline-block;width:calc(80% + 7px);padding-left:20px;padding-right:20px;margin-right:4px;" onclick="firmware.backup();">Backup</button>`;
                }
                html += `<button id="btnUpdate" type="button" style="width:40%;padding-left:20px;padding-right:20px;display:inline-block;margin-right:7px;" onclick="firmware.installGitRelease(document.getElementById('divGitInstall'));">Update</button>`;
                html += `<button id="btnClose" type="button" style="width:40%;padding-left:20px;padding-right:20px;display:inline-block;" onclick="document.getElementById('divGitInstall').remove();">Cancel</button>`;

                html += `</div></div>`;

                div.innerHTML = html;
                document.getElementById('divContainer').appendChild(div);
                this.gitReleaseSelected(div);
            }
        });
        
    }
    gitReleaseSelected(div) {
        let obj = ui.fromElement(div);
        let divNotes = div.querySelector('#divReleaseNotes');
        let divPre = div.querySelector('#divPrereleaseWarning');

        let sel = div.querySelector('#selVersion');
        if (sel && sel.selectedIndex !== -1 && makeBool(sel.options[sel.selectedIndex].getAttribute('data-prerelease'))) {
            if (divPre) divPre.style.display = '';
        }
        else
            if (divPre) divPre.style.display = 'none';

        if (divNotes) {
            if (!obj.version || obj.version === 'main' || obj.version === '') divNotes.style.display = 'none';
            else divNotes.style.display = '';
        }
    }
    async getReleaseInfo(tag) {
        let overlay = ui.waitMessage(document.getElementById('divContainer'));
        try {
            let ret = {};
            ret.resp = await fetch(`https://api.github.com/repos/rstrouse/espsomfy-rts/releases/tags/${tag}`);
            if (ret.resp.ok)
                ret.info = await ret.resp.json();
            return ret;
        }
        catch (err) {
            return { err: err };
        }
        finally { overlay.remove(); }
    }
    async showReleaseNotes(tagName) {
        console.log(tagName);
        let r = await this.getReleaseInfo(tagName);
        console.log(r);
        let fnToItem = (txt, tag) => {  }
        if (r.resp.ok) {
            // Convert this to html.
            let lines = r.info.body.split('\r\n');
            let ctx = { html: '', llvl: 0, lines: r.info.body.split('\r\n'), ndx: 0 };
            ctx.toHead = function (txt) {
                let num = txt.indexOf(' ');
                return `<h${num}>${txt.substring(num).trim()}</h${num}>`;
            };
            ctx.toUL = function () {
                let txt = this.lines[this.ndx++];
                let tok = this.token(txt);
                this.html += `<ul>${this.toLI(tok.txt)}`;
                while (this.ndx < this.lines.length) {
                    txt = this.lines[this.ndx];
                    let t = this.token(txt);
                    if (t.ch === '*') {
                        if (t.indent !== tok.indent) this.toUL();
                        else {
                            this.html += this.toLI(t.txt);
                            this.ndx++;
                        }
                    }
                    else break;
                }
                this.html += '</ul>';
            };
            ctx.toLI = function (txt) { return `<li>${txt.trim()}</li>`; }
            ctx.token = function (txt) {
                let tok = { ch: '', indent: 0, txt:'' }
                for (let i = 0; i < txt.length; i++) {
                    if (txt[i] === ' ') tok.indent++;
                    else {
                        tok.ch = txt[i];
                        let tmp = txt.substring(tok.indent);
                        tok.txt = tmp.substring(tmp.indexOf(' '));
                        break;
                    }
                }
                return tok;
            };
            ctx.next = function () {
                if (this.ndx >= this.lines.length) return false;
                let tok = this.token(this.lines[this.ndx]);
                switch (tok.ch) {
                    case '#':
                        this.html += this.toHead(this.lines[this.ndx]);
                        this.ndx++;
                        break;
                    case '*':
                        this.toUL();
                        break;
                    case '':
                        this.ndx++;
                        this.html += `<br/><div>${tok.txt}</div>`;
                        break;
                    default:
                        this.ndx++;
                        break;
                }
                return true;
            };
            while (ctx.next());
            console.log(ctx);
            ui.infoMessage(ctx.html);
        }
    }
    updateFirmware() {
        let div = this.createFileUploader('/updateFirmware');
        let inst = div.querySelector('div[id=divInstText]');
        let html = '<div style="font-size:14px;margin-bottom:20px;">Select a binary file [SomfyController.ino.esp32.bin] containing the device firmware then press the Upload File button.</div>';
        if (this.isMobile()) {
            html += `<div style="width:100%;color:red;text-align:center;font-weight:bold;"><span style="margin-top:7px;width:100%;background:yellow;padding:3px;display:inline-block;border-radius:5px;background:white;">WARNING<span></div>`;
            html += '<hr/><div style="font-size:14px;margin-bottom:10px;">This browser does not support automatic backups.  It is highly recommended that you back up your configuration using the backup button before proceeding.</div>';
        }
        else
            html += '<hr/><div style="font-size:14px;margin-bottom:10px;">A backup file for your configuration will be downloaded to your browser.  If the firmware update process fails please restore this file using the restore button after going through the onboarding process.</div>'
        inst.innerHTML = html;
        document.getElementById('divContainer').appendChild(div);
        if (this.isMobile()) document.getElementById('btnBackupCfg').style.display = 'inline-block';
    }
    updateApplication() {
        let div = this.createFileUploader('/updateApplication');
        general.reloadApp = true;
        let inst = div.querySelector('div[id=divInstText]');
        inst.innerHTML = '<div style="font-size:14px;">Select a binary file [SomfyController.littlefs.bin] containing the littlefs data for the application then press the Upload File button.</div>';
        if (this.isMobile()) {
            inst.innerHTML += `<div style="width:100%;color:red;text-align:center;font-weight:bold;"><span style="margin-top:7px;width:100%;background:yellow;padding:3px;display:inline-block;border-radius:5px;background:white;">WARNING<span></div>`;
            inst.innerHTML += '<hr/><div style="font-size:14px;margin-bottom:10px;">This browser does not support automatic backups.  It is highly recommended that you back up your configuration using the backup button before proceeding.</div>';
        }
        else
            inst.innerHTML += '<hr/><div style="font-size:14px;margin-bottom:10px;">A backup file for your configuration will be downloaded to your browser.  If the application update process fails please restore this file using the restore button</div>';
        document.getElementById('divContainer').appendChild(div);
        if(this.isMobile()) document.getElementById('btnBackupCfg').style.display = 'inline-block';
    }
    async uploadFile(service, el, data) {
        let field = el.querySelector('input[type="file"]');
        let filename = field.value;
        console.log(filename);
        let formData = new FormData();
        formData.append('file', field.files[0]);
        switch (service) {
            case '/updateApplication':
                if (typeof filename !== 'string' || filename.length === 0) {
                    ui.errorMessage('You must select a littleFS binary file to proceed.');
                    return;
                }
                else if (filename.indexOf('.littlefs') === -1 || !filename.endsWith('.bin')) {
                    ui.errorMessage('This file is not a valid littleFS binary file.');
                    return;
                }
                if (!this.isMobile()) {
                    console.log('Starting backup');
                    try {
                        await firmware.backup();
                        console.log('Backup Complete');
                    }
                    catch (err) {
                        ui.serviceError(el, err);
                        return;
                    }
                }
                break;
            case '/updateFirmware':
                if (typeof filename !== 'string' || filename.length === 0) {
                    ui.errorMessage('You must select a valid firmware binary file to proceed.');
                    return;
                }
                else if (filename.indexOf('.ino.') === -1 || !filename.endsWith('.bin')) {
                    ui.errorMessage(el, 'This file is not a valid firmware binary file.');
                    return;
                }
                if (!this.isMobile()) {
                    console.log('Starting backup');
                    try {
                        await firmware.backup();
                        console.log('Backup Complete');
                    }
                    catch(err) {
                        ui.serviceError(el, err);
                        return;
                    }
                }
                break;
            case '/restore':
                if (typeof filename !== 'string' || filename.length === 0) {
                    ui.errorMessage('You must select a valid backup file to proceed.');
                    return;
                }
                else if (field.files[0].size > 20480) {
                    ui.errorMessage(el, `This file is ${field.files[0].size.fmt("#,##0")} bytes in length.  This file is too large to be a valid backup file.`);
                    return;
                }
                else if (!filename.endsWith('.backup')) {
                    ui.errorMessage(el, 'This file is not a valid backup file');
                    return;
                }
                if (!data.shades && !data.settings && !data.network && !data.transceiver && !data.repeaters && !data.mqtt) {
                    ui.errorMessage(el, 'No restore options have been selected');
                    return;
                }
                console.log(data);
                formData.append('data', JSON.stringify(data));
                console.log(formData.get('data'));
                //return;
                break;
        }
        let btnUpload = el.querySelector('button[id="btnUploadFile"]');
        let btnCancel = el.querySelector('button[id="btnClose"]');
        let btnBackup = el.querySelector('button[id="btnBackupCfg"]');
        btnBackup.style.display = 'none';
        btnUpload.style.display = 'none';
        field.disabled = true;
        let btnSelectFile = el.querySelector('div[id="btn-select-file"]');
        let prog = el.querySelector('div[id="progFileUpload"]');
        prog.style.display = '';
        btnSelectFile.style.visibility = 'hidden';
        let xhr = new XMLHttpRequest();
        //xhr.open('POST', service, true);
        xhr.open('POST', baseUrl.length > 0 ? `${baseUrl}${service}` : service, true);
        xhr.upload.onprogress = function (evt) {
            let pct = evt.total ? Math.round((evt.loaded / evt.total) * 100) : 0;
            prog.style.setProperty('--progress', `${pct}%`);
            prog.setAttribute('data-progress', `${pct}%`);
            console.log(evt);
            
        };
        xhr.onerror = function (err) {
            console.log(err);
            ui.serviceError(el, err);
        };
        xhr.onload = function () {
            console.log('File upload load called');
            btnCancel.innerText = 'Close';
            switch (service) {
                case '/restore':
                    (async () => {
                        await somfy.init();
                        if (document.getElementById('divUploadFile')) document.getElementById('divUploadFile').remove();
                    })();
                    break;
                case '/updateApplication':

                    break;
            }
        };
        xhr.send(formData);
        btnCancel.addEventListener('click', (e) => {
            console.log('Cancel clicked');
            xhr.abort();
        });

    }
}
var firmware = new Firmware();

class HomeKit {
    load() {
        getJSONSync('/homekit', (err, data) => {
            if (err) { ui.serviceError(err); return; }
            let notEnabled = document.getElementById('divHomeKitNotEnabled');
            let content    = document.getElementById('divHomeKitContent');
            if (!data.started) {
                if (notEnabled) notEnabled.style.display = '';
                if (content)    content.style.display    = 'none';
                return;
            }
            if (notEnabled) notEnabled.style.display = 'none';
            if (content)    content.style.display    = '';
            let code = document.getElementById('spanHKSetupCode');
            if (code) code.innerHTML = data.setupCode || '---';
            let qrdiv = document.getElementById('divHKQR');
            if (qrdiv && data.qrPayload && typeof qrcode !== 'undefined') {
                try {
                    var qr = qrcode(0, 'M');
                    qr.addData(data.qrPayload);
                    qr.make();
                    qrdiv.innerHTML = qr.createSvgTag(4, 2);
                    var svg = qrdiv.querySelector('svg');
                    if (svg) { svg.style.background = '#fff'; svg.style.borderRadius = '4px'; }
                } catch(e) { console.error('QR error:', e); }
            }
            let count = document.getElementById('spanHKPairedCount');
            if (count) {
                let n = data.pairedCount || 0;
                count.innerHTML = n === 0 ? 'None' : n === 1 ? '1 controller' : `${n} controllers`;
            }
        });
    }
    resetPairings() {
        if (!confirm('Remove all paired HomeKit controllers? You will need to re-pair all devices.')) return;
        postJSONSync('/homekit/resetPairings', {}, (err) => {
            if (err) ui.serviceError(err);
            else {
                let count = document.getElementById('spanHKPairedCount');
                if (count) count.innerHTML = 'None';
                ui.infoMessage('All HomeKit pairings have been reset.');
            }
        });
    }
}
var hkit = new HomeKit();

