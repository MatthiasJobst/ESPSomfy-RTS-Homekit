class Somfy {
    initialized = false;
    frames = [];
    shadeTypes = [
        { type: 0, name: 'Roller Shade', ico: 'icss-window-shade', lift: true, sun: true, fcmd: true, fpos: true },
        { type: 1, name: 'Blind', ico: 'icss-window-blind', lift: true, tilt: true, sun: true, fcmd: true, fpos: true },
        { type: 2, name: 'Drapery (left)', ico: 'icss-ldrapery', lift: true, sun: true, fcmd: true, fpos: true },
        { type: 3, name: 'Awning', ico: 'icss-awning', lift: true, sun: true, fcmd: true, fpos: true },
        { type: 4, name: 'Shutter', ico: 'icss-shutter', lift: true, sun: true, fcmd: true, fpos: true },
        { type: 5, name: 'Garage (1-button)', ico: 'icss-garage', lift: true, light: true, fpos: true },
        { type: 6, name: 'Garage (3-button)', ico: 'icss-garage', lift: true, light: true, fcmd: true, fpos: true },
        { type: 7, name: 'Drapery (right)', ico: 'icss-rdrapery', lift: true, sun: true, fcmd: true, fpos: true },
        { type: 8, name: 'Drapery (center)', ico: 'icss-cdrapery', lift: true, sun: true, fcmd: true, fpos: true },
        { type: 9, name: 'Dry Contact (1-button)', ico: 'icss-lightbulb', fpos: true },
        { type: 10, name: 'Dry Contact (2-button)', ico: 'icss-lightbulb', fcmd: true, fpos: true },
        { type: 11, name: 'Gate (left)', ico: 'icss-lgate', lift: true, fcmd: true, fpos: true },
        { type: 12, name: 'Gate (center)', ico: 'icss-cgate', lift: true, fcmd: true, fpos: true },
        { type: 13, name: 'Gate (right)', ico: 'icss-rgate', lift: true, fcmd: true, fpos: true },
        { type: 14, name: 'Gate (1-button left)', ico: 'icss-lgate', lift: true, fcmd: true, fpos: true },
        { type: 15, name: 'Gate (1-button center)', ico: 'icss-cgate', lift: true, fcmd: true, fpos: true },
        { type: 16, name: 'Gate (1-button right)', ico: 'icss-rgate', lift: true, fcmd: true, fpos: true },
    ];
    init() {
        if (this.initialized) return;
        this.initialized = true;
    }
    initPins() {
        this.loadPins('inout', document.getElementById('selTransSCKPin'));
        this.loadPins('inout', document.getElementById('selTransCSNPin'));
        this.loadPins('inout', document.getElementById('selTransMOSIPin'));
        this.loadPins('input', document.getElementById('selTransMISOPin'));
        this.loadPins('out', document.getElementById('selTransTXPin'));
        this.loadPins('input', document.getElementById('selTransRXPin'));
        //this.loadSomfy();
        ui.toElement(document.getElementById('divTransceiverSettings'), {
            transceiver: { config: { proto: 0, SCKPin: 18, CSNPin: 5, MOSIPin: 23, MISOPin: 19, TXPin: 12, RXPin: 13, frequency: 433.42, rxBandwidth: 97.96, type: 56, deviation: 11.43, txPower: 10, enabled: false } }
        });
        this.loadPins('out', document.getElementById('selShadeGPIOUp'));
        this.loadPins('out', document.getElementById('selShadeGPIODown'));
        this.loadPins('out', document.getElementById('selShadeGPIOMy'));
    }
    async loadSomfy() {
        return getJSONSync('/controller', (err, somfy) => {
            if (err) {
                console.log(err);
                ui.serviceError(err);
            }
            else {
                console.log(somfy);
                document.getElementById('spanMaxRooms').innerText = somfy.maxRooms || 0;
                document.getElementById('spanMaxShades').innerText = somfy.maxShades;
                document.getElementById('spanMaxGroups').innerText = somfy.maxGroups;
                ui.toElement(document.getElementById('divTransceiverSettings'), somfy);
                if (somfy.transceiver.config.radioInit) {
                    document.getElementById('divRadioError').style.display = 'none';
                }
                else {
                    document.getElementById('divRadioError').style.display = '';
                }
                // Create the shades list.
                this.setRoomsList(somfy.rooms);
                this.setShadesList(somfy.shades);
                this.setGroupsList(somfy.groups);
                this.setRepeaterList(somfy.repeaters);
                if (typeof somfy.version !== 'undefined') firmware.procFwStatus(somfy.version);
            }
        });
    }
    saveRadio() {
        let valid = true;
        let getIntValue = (fld) => { return parseInt(document.getElementById(fld).value, 10); };
        let trans = ui.fromElement(document.getElementById('divTransceiverSettings')).transceiver;
        // Check to make sure we have a trans type.
        if (typeof trans.config.type === 'undefined' || trans.config.type === '' || trans.config.type === 'none') {
            ui.errorMessage('You must select a radio type.');
            valid = false;
        }
        // Check to make sure no pins were duplicated and defined
        if (valid) {
            let fnValDup = (o, name) => {
                let val = o[name];
                if (typeof val === 'undefined' || isNaN(val)) {
                    ui.errorMessage(document.getElementById('divSomfySettings'), 'You must define all the pins for the radio.');
                    return false;
                }
                for (let s in o) {
                    if (s.endsWith('Pin') && s !== name) {
                        let sval = o[s];
                        if (typeof sval === 'undefined' || isNaN(sval)) {
                            ui.errorMessage(document.getElementById('divSomfySettings'), 'You must define all the pins for the radio.');
                            return false;
                        }
                        if (sval === val) {
                            if ((name === 'TXPin' && s === 'RXPin') ||
                                (name === 'RXPin' && s === 'TXPin'))
                                continue; // The RX and TX pins can share the same value.  In this instance the radio will only use GDO0.
                            else {
                                ui.errorMessage(document.getElementById('divSomfySettings'), `The ${name.replace('Pin', '')} pin is duplicated by the ${s.replace('Pin', '')}.  All pin definitions must be unique`);
                                valid = false;
                                return false;
                            }
                        }
                    }
                }
                return true;
            };
            if (valid) valid = fnValDup(trans.config, 'SCKPin');
            if (valid) valid = fnValDup(trans.config, 'CSNPin');
            if (valid) valid = fnValDup(trans.config, 'MOSIPin');
            if (valid) valid = fnValDup(trans.config, 'MISOPin');
            if (valid) valid = fnValDup(trans.config, 'TXPin');
            if (valid) valid = fnValDup(trans.config, 'RXPin');
            if (valid) {
                putJSONSync('/saveRadio', trans, (err, trans) => {
                    if (err)
                        ui.serviceError(err);
                    else {
                        document.getElementById('btnSaveRadio').classList.remove('disabled');
                        if (trans.config.radioInit) {
                            document.getElementById('divRadioError').style.display = 'none';
                        }
                        else {
                            document.getElementById('divRadioError').style.display = '';
                        }
                    }
                    console.log(trans);
                });
            }
        }
    }
    procFrequencyScan(scan) {
        //console.log(scan);
        let div = this.scanFrequency();
        let spanTestFreq = document.getElementById('spanTestFreq');
        let spanTestRSSI = document.getElementById('spanTestRSSI');
        let spanBestFreq = document.getElementById('spanBestFreq');
        let spanBestRSSI = document.getElementById('spanBestRSSI');
        if (spanBestFreq) {
            spanBestFreq.innerHTML = scan.RSSI !== -100 ? scan.frequency.fmt('###.00') : '----';
        }
        if (spanBestRSSI) {
            spanBestRSSI.innerHTML = scan.RSSI !== -100 ? scan.RSSI : '----';
        }
        if (spanTestFreq) {
            spanTestFreq.innerHTML = scan.testFreq.fmt('###.00');
        }
        if (spanTestRSSI) {
            spanTestRSSI.innerHTML = scan.testRSSI !== -100 ? scan.testRSSI : '----';
        }
        if (scan.RSSI !== -100)
            div.setAttribute('data-frequency', scan.frequency);
    }
    scanFrequency(initScan) {
        let div = document.getElementById('divScanFrequency');
        if (!div || typeof div === 'undefined') {
            div = document.createElement('div');
            div.setAttribute('id', 'divScanFrequency');
            div.classList.add('prompt-message');
            let html = '<div class="sub-message">Frequency Scanning has started.  Press and hold any button on your remote and ESPSomfy RTS will find the closest frequency to the remote.</div>';
            html += '<hr style="width:100%;margin:0px;"></hr>';
            html += '<div style="width:100%;text-align:center;">';
            html += '<div class="" style="font-size:20px;"><label style="padding-right:7px;display:inline-block;width:87px;">Scanning</label><span id="spanTestFreq" style="display:inline-block;width:4em;text-align:right;">433.00</span><span>MHz</span><label style="padding-left:12px;padding-right:7px;">RSSI</label><span id="spanTestRSSI">----</span><span>dBm</span></div>';
            html += '<div class="" style="font-size:20px;"><label style="padding-right:7px;display:inline-block;width:87px;">Frequency</label><span id="spanBestFreq" style="display:inline-block;width:4em;text-align:right;">---.--</span><span>MHz</span><label style="padding-left:12px;padding-right:7px;">RSSI</label><span id="spanBestRSSI">----</span><span>dBm</span></div>';
            html += '</div>';
            html += `<div class="button-container">`;
            html += `<button id="btnStopScanning" type="button" style="padding-left:20px;padding-right:20px;" onclick="somfy.stopScanningFrequency(true);">Stop Scanning</button>`;
            html += `<button id="btnRestartScanning" type="button" style="padding-left:20px;padding-right:20px;display:none;" onclick="somfy.scanFrequency(true);">Start Scanning</button>`;
            html += `<button id="btnCopyFrequency" type="button" style="padding-left:20px;padding-right:20px;display:none;" onclick="somfy.setScannedFrequency();">Set Frequency</button>`;
            html += `<button id="btnCloseScanning" type="button" style="padding-left:20px;padding-right:20px;width:100%;display:none;" onclick="document.getElementById('divScanFrequency').remove();">Close</button>`;
            html += `</div>`;
            div.innerHTML = html;
            document.getElementById('divRadioSettings').appendChild(div);
        }
        if (initScan) {
            div.setAttribute('data-initscan', true);
            putJSONSync('/beginFrequencyScan', {}, (err, trans) => {
                if (!err) {
                    document.getElementById('btnStopScanning').style.display = '';
                    document.getElementById('btnRestartScanning').style.display = 'none';
                    document.getElementById('btnCopyFrequency').style.display = 'none';
                    document.getElementById('btnCloseScanning').style.display = 'none';
                }
            });
        }
        return div;
    }
    setScannedFrequency() {
        let div = document.getElementById('divScanFrequency');
        let freq = parseFloat(div.getAttribute('data-frequency'));
        let slid = document.getElementById('slidFrequency');
        slid.value = Math.round(freq * 1000);
        somfy.frequencyChanged(slid);
        div.remove();
    }
    stopScanningFrequency(killScan) {
        let div = document.getElementById('divScanFrequency');
        if (div && killScan !== true) {
            div.remove();
        }
        else {
            putJSONSync('/endFrequencyScan', {}, (err, trans) => {
                if (err) ui.serviceError(err);
                else {
                    let freq = parseFloat(div.getAttribute('data-frequency'));
                    document.getElementById('btnStopScanning').style.display = 'none';
                    document.getElementById('btnRestartScanning').style.display = '';
                    if (typeof freq === 'number' && !isNaN(freq)) document.getElementById('btnCopyFrequency').style.display = '';
                    document.getElementById('btnCloseScanning').style.display = '';
                }
            });
        }
    }
    btnDown = null;
    btnTimer = null;
    procRoomAdded(room) {
        let r = _rooms.find(x => x.roomId === room.roomId);
        if (typeof r === 'undefined' || !r) {
            _rooms.push(room);
            _rooms.sort((a, b) => { return a.sortOrder - b.sortOrder });
            this.setRoomsList(_rooms);
        }
    }
    procRoomRemoved(room) {
        if (room.roomId === 0) return;
        let r = _rooms.find(x => x.roomId === room.roomId);
        if (typeof r !== 'undefined' && r.roomId === room.roomId) {
            _rooms = _rooms.filter(x => x.roomId === room.roomId);
            _rooms.sort((a, b) => { return a.sortOrder - b.sortOrder });
            this.setRoomsList(_rooms);
            let rs = document.getElementById('divRoomSelector');
            let ss = document.getElementById('divShadeControls');
            let gs = document.getElementById('divGroupControls');
            let ctls = ss.querySelectorAll('.somfyShadeCtl');
            for (let i = 0; i < ctls.length; i++) {
                let x = ctls[i];
                if (parseInt(x.getAttribute('data-roomid'), 10) === room.roomId)
                    x.setAttribute('data-roomid', '0');
            }
            ctls = gs.querySelectorAll('.somfyGroupCtl');
            for (let i = 0; i < ctls.length; i++) {
                let x = ctls[i];
                if (parseInt(x.getAttribute('data-roomid'), 10) === room.roomId)
                    x.setAttribute('data-roomid', '0');
            }
            if (parseInt(rs.getAttribute('data-roomid'), 10) === room.roomId) this.selectRoom(0);
        }
    }
    selectRoom(roomId) {
        let room = _rooms.find(x => x.roomId === roomId) || { roomId: 0, name: '' };
        let rs = document.getElementById('divRoomSelector');
        rs.setAttribute('data-roomid', roomId);
        rs.querySelector('span').innerHTML = room.name;
        document.getElementById('divRoomSelector-list').style.display = 'none';
        let ss = document.getElementById('divShadeControls');
        ss.setAttribute('data-roomid', roomId);
        let ctls = ss.querySelectorAll('.somfyShadeCtl');
        for (let i = 0; i < ctls.length; i++) {
            let x = ctls[i];
            if (roomId !== 0 && parseInt(x.getAttribute('data-roomid'), 10) !== roomId)
                x.style.display = 'none';
            else
                x.style.display = '';
        }
        let gs = document.getElementById('divGroupControls');
        ctls = gs.querySelectorAll('.somfyGroupCtl');
        for (let i = 0; i < ctls.length; i++) {
            let x = ctls[i];
            if (roomId !== 0 && parseInt(x.getAttribute('data-roomid'), 10) !== roomId)
                x.style.display = 'none';
            else
                x.style.display = '';
        }
    }
    setRoomsList(rooms) {
        let divCfg = '';
        let divCtl = `<div class='room-row' data-roomid="${0}" onclick="somfy.selectRoom(0);event.stopPropagation();">Home</div>`;
        let divOpts = '<option value="0">Home</option>';
        _rooms = [{ roomId: 0, name: 'Home' }];
        rooms.sort((a, b) => { return a.sortOrder - b.sortOrder });
        for (let i = 0; i < rooms.length; i++) {
            let room = rooms[i];
            divCfg += `<div class="somfyRoom room-draggable" draggable="true" data-roomid="${room.roomId}">`;
            divCfg += `<div class="button-outline" onclick="somfy.openEditRoom(${room.roomId});"><i class="icss-edit"></i></div>`;
            divCfg += `<span class="room-name">${room.name}</span>`;
            divCfg += `<div class="button-outline" onclick="somfy.deleteRoom(${room.roomId});"><i class="icss-trash"></i></div>`;
            divCfg += '</div>';
            divOpts += `<option value="${room.roomId}">${room.name}</option>`;
            _rooms.push(room);
            divCtl += `<div class='room-row' data-roomid="${room.roomId}" onclick="somfy.selectRoom(${room.roomId});event.stopPropagation();">${room.name}</div>`;
        }
        document.getElementById('divRoomSelector').style.display = rooms.length === 0 ? 'none' : '';
        document.getElementById('divRoomSelector-list').innerHTML = divCtl;
        document.getElementById('divRoomList').innerHTML = divCfg;
        document.getElementById('selShadeRoom').innerHTML = divOpts;
        document.getElementById('selGroupRoom').innerHTML = divOpts;
        //roomControls.innerHTML = divCtl;
        this.setListDraggable(document.getElementById('divRoomList'), '.room-draggable', (list) => {
            // Get the shade order
            let items = list.querySelectorAll('.room-draggable');
            let order = [];
            for (let i = 0; i < items.length; i++) {
                order.push(parseInt(items[i].getAttribute('data-roomid'), 10));
                // Reorder the shades on the main page.
            }
            putJSONSync('/roomSortOrder', order, (err) => {
                if (err) ui.serviceError(err);
                else this.updateRoomsList();
            });
        });
    }
    setRepeaterList(addresses) {
        let divCfg = '';
        if (typeof addresses !== 'undefined') {
            for (let i = 0; i < addresses.length; i++) {
                divCfg += `<div class="somfyRepeater" data-address="${addresses[i]}"><div class="repeater-name">${addresses[i]}</div>`;
                divCfg += `<div class="button-outline" onclick="somfy.unlinkRepeater(${addresses[i]});"><i class="icss-trash"></i></div>`;
                divCfg += '</div>';
            }
        }
        document.getElementById('divRepeatList').innerHTML = divCfg;

    }
    setShadesList(shades) {
        let divCfg = '';
        let divCtl = '';
        shades.sort((a, b) => { return a.sortOrder - b.sortOrder });
        console.log(shades);
        let roomId = parseInt(document.getElementById('divRoomSelector').getAttribute('data-roomid'), 10)

        let vrList = document.getElementById('selVRMotor');
        // First get the optiongroup for the shades.
        let optGroup = document.getElementById('optgrpVRShades');
        if (typeof shades === 'undefined' || shades.length === 0) {
            if (optGroup && typeof optGroup !== 'undefined') optGroup.remove();
        }
        else {
            if (typeof optGroup === 'undefined' || !optGroup) {
                optGroup = document.createElement('optgroup');
                optGroup.setAttribute('id', 'optgrpVRShades');
                optGroup.setAttribute('label', 'Shades');
                vrList.appendChild(optGroup);
            }
            else {
                optGroup.innerHTML = '';
            }
        }
        for (let i = 0; i < shades.length; i++) {
            let shade = shades[i];
            let room = _rooms.find(x => x.roomId === shade.roomId) || { roomId: 0, name: '' };
            let st = this.shadeTypes.find(x => x.type === shade.shadeType) || { type: shade.shadeType, ico: 'icss-window-shade' };

            divCfg += `<div class="somfyShade shade-draggable" draggable="true" data-roomid="${shade.roomId}" data-mypos="${shade.myPos}" data-shadeid="${shade.shadeId}" data-remoteaddress="${shade.remoteAddress}" data-tilt="${shade.tiltType}" data-shadetype="${shade.shadeType} data-flipposition="${shade.flipPosition ? 'true' : 'false'}">`;
            divCfg += `<div class="button-outline" onclick="somfy.openEditShade(${shade.shadeId});"><i class="icss-edit"></i></div>`;
            //divCfg += `<i class="shade-icon" data-position="${shade.position || 0}%"></i>`;
            //divCfg += `<span class="shade-name">${shade.name}</span>`;
            divCfg += '<div class="shade-name">';
            divCfg += `<div class="cfg-room">${room.name}</div>`;
            divCfg += `<div class="">${shade.name}</div>`;
            divCfg += '</div>'

            divCfg += `<span class="shade-address">${shade.remoteAddress}</span>`;
            divCfg += `<div class="button-outline" onclick="somfy.deleteShade(${shade.shadeId});"><i class="icss-trash"></i></div>`;
            divCfg += '</div>';

            divCtl += `<div class="somfyShadeCtl" style="${roomId === 0 || roomId === room.roomId ? '' : 'display:none'}" data-shadeId="${shade.shadeId}" data-roomid="${shade.roomId}" data-direction="${shade.direction}" data-remoteaddress="${shade.remoteAddress}" data-position="${shade.position}" data-target="${shade.target}" data-mypos="${shade.myPos}" data-mytiltpos="${shade.myTiltPos}" data-shadetype="${shade.shadeType}" data-tilt="${shade.tiltType}" data-flipposition="${shade.flipPosition ? 'true' : 'false'}"`;
            divCtl += ` data-windy="${(shade.flags & 0x10) === 0x10 ? 'true' : 'false'}" data-sunny=${(shade.flags & 0x20) === 0x20 ? 'true' : 'false'}`;
            if (shade.tiltType !== 0) {
                divCtl += ` data-tiltposition="${shade.tiltPosition}" data-tiltdirection="${shade.tiltDirection}" data-tilttarget="${shade.tiltTarget}"`;
            }
            divCtl += `><div class="shade-icon" data-shadeid="${shade.shadeId}" onclick="event.stopPropagation(); console.log(event); somfy.openSetPosition(${shade.shadeId});">`;
            divCtl += `<i class="somfy-shade-icon ${st.ico}`;
            divCtl += `" data-shadeid="${shade.shadeId}" style="--shade-position:${shade.flipPosition ? 100 - shade.position : shade.position}%;--fpos:${shade.position}%;vertical-align: top;"><span class="icss-panel-left"></span><span class="icss-panel-right"></span></i>`;
            //divCtl += `" data-shadeid="${shade.shadeId}" style="--shade-position:${shade.position}%;vertical-align: top;"><span class="icss-panel-left"></span><span class="icss-panel-right"></span></i>`;

            divCtl += shade.tiltType !== 0 ? `<i class="icss-window-tilt" data-shadeid="${shade.shadeId}" data-tiltposition="${shade.tiltPosition}"></i></div>` : '</div>';
            divCtl += `<div class="indicator indicator-wind"><i class="icss-warning"></i></div><div class="indicator indicator-sun"><i class="icss-sun"></i></div>`;
            divCtl += `<div class="shade-name">`;
            divCtl += `<span class="shadectl-room">${room.name}</span>`;
            divCtl += `<span class="shadectl-name">${shade.name}</span>`;
            divCtl += `<span class="shadectl-mypos"><label class="my-pos"></label><span class="my-pos">${shade.myPos === -1 ? '---' : shade.myPos + '%'}</span><label class="my-pos-tilt"></label><span class="my-pos-tilt">${shade.myTiltPos === -1 ? '---' : shade.myTiltPos + '%'}</span >`;
            divCtl += '</div>';
            divCtl += `<div class="shadectl-buttons" data-shadeType="${shade.shadeType}">`;
            divCtl += `<div class="button-light cmd-button" data-cmd="light" data-shadeid="${shade.shadeId}" data-on="${shade.flags & 0x08 ? 'true' : 'false'}" style="${!shade.light ? 'display:none' : ''}"><i class="icss-lightbuld-c"></i><i class="icss-lightbulb-o"></i></div>`;
            divCtl += `<div class="button-sunflag cmd-button" data-cmd="sunflag" data-shadeid="${shade.shadeId}" data-on="${shade.flags & 0x01 ? 'true' : 'false'}" style="${!shade.sunSensor ? 'display:none' : ''}"><i class="icss-sun-c"></i><i class="icss-sun-o"></i></div>`;
            divCtl += `<div class="button-outline cmd-button" data-cmd="up" data-shadeid="${shade.shadeId}"><i class="icss-somfy-up"></i></div>`;
            divCtl += `<div class="button-outline cmd-button my-button" data-cmd="my" data-shadeid="${shade.shadeId}" style="font-size:2em;padding:10px;"><span>my</span></div>`;
            divCtl += `<div class="button-outline cmd-button" data-cmd="down" data-shadeid="${shade.shadeId}"><i class="icss-somfy-down" style="margin-top:-4px;"></i></div>`;
            divCtl += `<div class="button-outline cmd-button toggle-button" style="width:127px;text-align:center;border-radius:33%;font-size:2em;padding:10px;" data-cmd="toggle" data-shadeid="${shade.shadeId}"><i class="icss-somfy-toggle" style="margin-top:-4px;"></i></div>`;
            divCtl += '</div></div>';
            divCtl += '</div>';
            let opt = document.createElement('option');
            opt.innerHTML = shade.name;
            opt.setAttribute('data-address', shade.remoteAddress);
            opt.setAttribute('data-type', 'shade');
            opt.setAttribute('data-shadetype', shade.shadeType);
            opt.setAttribute('data-shadeid', shade.shadeId);
            opt.setAttribute('data-bitlength', shade.bitLength);
            optGroup.appendChild(opt);
        }
        let sopt = vrList.options[vrList.selectedIndex];
        document.getElementById('divVirtualRemote').setAttribute('data-bitlength', sopt ? sopt.getAttribute('data-bitlength') : 'none');
        document.getElementById('divShadeList').innerHTML = divCfg;
        let shadeControls = document.getElementById('divShadeControls');
        shadeControls.innerHTML = divCtl;
        // Attach the timer for setting the My Position for the shade.
        let btns = shadeControls.querySelectorAll('div.cmd-button');
        for (let i = 0; i < btns.length; i++) {
            btns[i].addEventListener('mouseup', (event) => {
                console.log(this);
                console.log(event);
                console.log('mouseup');
                let cmd = event.currentTarget.getAttribute('data-cmd');
                let shadeId = parseInt(event.currentTarget.getAttribute('data-shadeid'), 10);
                if (this.btnTimer) {
                    console.log({ timer: true, isOn: event.currentTarget.getAttribute('data-on'), cmd: cmd });
                    clearTimeout(this.btnTimer);
                    this.btnTimer = null;
                    if (new Date().getTime() - this.btnDown > 2000) event.preventDefault();
                    else this.sendCommand(shadeId, cmd);
                }
                else if (cmd === 'light') {
                    event.currentTarget.setAttribute('data-on', !makeBool(event.currentTarget.getAttribute('data-on')));
                }
                else if (cmd === 'sunflag') {
                    if (makeBool(event.currentTarget.getAttribute('data-on')))
                        this.sendCommand(shadeId, 'flag');
                    else
                        this.sendCommand(shadeId, 'sunflag');
                }
                else this.sendCommand(shadeId, cmd);
            }, true);
            btns[i].addEventListener('mousedown', (event) => {
                if (this.btnTimer) {
                    clearTimeout(this.btnTimer);
                    this.btnTimer = null;
                }
                console.log(this);
                console.log(event);
                console.log('mousedown');
                let elShade = event.currentTarget.closest('div.somfyShadeCtl');
                let cmd = event.currentTarget.getAttribute('data-cmd');
                let shadeId = parseInt(event.currentTarget.getAttribute('data-shadeid'), 10);
                let el = event.currentTarget.closest('.somfyShadeCtl');
                this.btnDown = new Date().getTime();
                if (cmd === 'my') {
                    if (parseInt(el.getAttribute('data-direction'), 10) === 0) {
                        this.btnTimer = setTimeout(() => {
                            // Open up the set My Position dialog.  We will allow the user to change the position to match
                            // the desired position.
                            this.openSetMyPosition(shadeId);
                        }, 2000);
                    }
                }
                else if (cmd === 'light') return;
                else if (cmd === 'sunflag') return;
                else if (makeBool(elShade.getAttribute('data-tilt'))) {
                    this.btnTimer = setTimeout(() => {
                        this.sendTiltCommand(shadeId, cmd);
                    }, 2000);
                }
            }, true);
            btns[i].addEventListener('touchstart', (event) => {
                if (this.btnTimer) {
                    clearTimeout(this.btnTimer);
                    this.btnTimer = null;
                }
                console.log(this);
                console.log(event);
                console.log('touchstart');
                let elShade = event.currentTarget.closest('div.somfyShadeCtl');
                let cmd = event.currentTarget.getAttribute('data-cmd');
                let shadeId = parseInt(event.currentTarget.getAttribute('data-shadeid'), 10);
                let el = event.currentTarget.closest('.somfyShadeCtl');
                this.btnDown = new Date().getTime();
                if (parseInt(el.getAttribute('data-direction'), 10) === 0) {
                    if (cmd === 'my') {
                        this.btnTimer = setTimeout(() => {
                            // Open up the set My Position dialog.  We will allow the user to change the position to match
                            // the desired position.
                            this.openSetMyPosition(shadeId);
                        }, 2000);
                    }
                    else {
                        if (makeBool(elShade.getAttribute('data-tilt'))) {
                            this.btnTimer = setTimeout(() => {
                                this.sendTiltCommand(shadeId, cmd);
                            }, 2000);
                        }
                    }
                }
            }, true);
        }
        this.setListDraggable(document.getElementById('divShadeList'), '.shade-draggable', (list) => {
            // Get the shade order
            let items = list.querySelectorAll('.shade-draggable');
            let order = [];
            for (let i = 0; i < items.length; i++) {
                order.push(parseInt(items[i].getAttribute('data-shadeid'), 10));
                // Reorder the shades on the main page.
            }
            putJSONSync('/shadeSortOrder', order, (err) => {
                for (let i = order.length - 1; i >= 0; i--) {
                    let el = shadeControls.querySelector(`.somfyShadeCtl[data-shadeid="${order[i]}"`);
                    if (el) {
                        shadeControls.prepend(el);
                    }
                }
            });
        });
    }
    setListDraggable(list, itemclass, onChanged) {
        let items = list.querySelectorAll(itemclass);
        let changed = false;
        let timerStart = null;
        let dragDiv = null;
        let fnDragStart = function (e) {
            //console.log({ evt: 'dragStart', e: e, this: this });
            if (typeof e.dataTransfer !== 'undefined') {
                e.dataTransfer.effectAllowed = 'move';
                e.dataTransfer.setData('text/html', this.innerHTML);
                this.style.opacity = '0.4';
                this.classList.add('dragging');
            }
            else {
                timerStart = setTimeout(() => {
                    this.style.opacity = '0.4';
                    dragDiv = document.createElement('div');
                    dragDiv.innerHTML = this.innerHTML;
                    dragDiv.style.position = 'absolute';
                    dragDiv.classList.add('somfyShade');
                    dragDiv.style.left = `${this.offsetLeft}px`;
                    dragDiv.style.width = `${this.clientWidth}px`;
                    dragDiv.style.top = `${this.offsetTop}px`;
                    dragDiv.style.border = 'dotted 1px silver';
                    //dragDiv.style.background = 'gainsboro';
                    list.appendChild(dragDiv);
                    this.classList.add('dragging');
                    timerStart = null;
                }, 1000);
            }
            e.stopPropagation();
        };
        let fnDragEnter = function (e) {
            //console.log({ evt: 'dragEnter', e: e, this: this });
            this.classList.add('over');
        };
        let fnDragOver = function (e) {
            //console.log({ evt: 'dragOver', e: e, this: this });
            if (timerStart) {
                clearTimeout(timerStart);
                timerStart = null;
                return;
            }
            e.preventDefault();
            if (typeof e.dataTransfer !== 'undefined') e.dataTransfer.dropEffect = 'move';
            else if (dragDiv) {
                let rc = list.getBoundingClientRect();
                let pageY = e.targetTouches[0].pageY;
                let y = pageY - rc.top;
                if (y < 0) y = 0;
                else if (y > rc.height) y = rc.height;
                dragDiv.style.top = `${y}px`;
                // Now lets calculate which element we are over.
                let ndx = -1;
                for (let i = 0; i < items.length; i++) {
                    let irc = items[i].getBoundingClientRect();
                    if (pageY <= irc.bottom - (irc.height / 2)) {
                        ndx = i;
                        break;
                    }
                }
                let over = items[ndx];
                if (ndx < 0) [].forEach.call(items, (item) => { item.classList.remove('over') });
                else if (!over.classList.contains['over']) {
                    [].forEach.call(items, (item) => { item.classList.remove('over') });
                    over.classList.add('over');
                }
            }
            return false;
        };
        let fnDragLeave = function (e) {
            console.log({ evt: 'dragLeave', e: e, this: this });
            this.classList.remove('over');
        };
        let fnDrop = function (e) {
            // Shift around the items.
            console.log({ evt: 'drop', e: e, this: this });
            let elDrag = list.querySelector('.dragging');
            if (elDrag !== this) {
                let curr = 0, end = 0;
                for (let i = 0; i < items.length; i++) {
                    if (this === items[i]) end = i;
                    if (elDrag === items[i]) curr = i;
                }
                if (curr !== end) {
                    this.before(elDrag);
                    changed = true;
                }
            }
        };
        let fnDragEnd = function (e) {
            console.log({ evt: 'dragEnd', e: e, this: this });
            let elOver = list.querySelector('.over');
            [].forEach.call(items, (item) => { item.classList.remove('over') });
            this.style.opacity = '1';
            //overCounter = 0;
            if (timerStart) {
                clearTimeout(timerStart);
                timerStart = null;
            }
            if (dragDiv) {
                dragDiv.remove();
                dragDiv = null;
                if (elOver && typeof elOver !== 'undefined') fnDrop.call(elOver, e);
            }
            if (changed && typeof onChanged === 'function') {
                onChanged(list);
            }
            this.classList.remove('dragging');
        };
        [].forEach.call(items, (item) => {
            if (firmware.isMobile()) {
                item.addEventListener('touchstart', fnDragStart);
                //item.addEventListener('touchenter', fnDragEnter);
                item.addEventListener('touchmove', fnDragOver);
                item.addEventListener('touchleave', fnDragLeave);
                item.addEventListener('drop', fnDrop);
                item.addEventListener('touchend', fnDragEnd);

            }
            else {
                item.addEventListener('dragstart', fnDragStart);
                item.addEventListener('dragenter', fnDragEnter);
                item.addEventListener('dragover', fnDragOver);
                item.addEventListener('dragleave', fnDragLeave);
                item.addEventListener('drop', fnDrop);
                item.addEventListener('dragend', fnDragEnd);
            }
        });
    }
    setGroupsList(groups) {
        let divCfg = '';
        let divCtl = '';
        let vrList = document.getElementById('selVRMotor');
        // First get the optiongroup for the shades.
        let optGroup = document.getElementById('optgrpVRGroups');
        if (typeof groups === 'undefined' || groups.length === 0) {
            if (optGroup && typeof optGroup !== 'undefined') optGroup.remove();
        }
        else {
            if (typeof optGroup === 'undefined' || !optGroup) {
                optGroup = document.createElement('optgroup');
                optGroup.setAttribute('id', 'optgrpVRGroups');
                optGroup.setAttribute('label', 'Groups');
                vrList.appendChild(optGroup);
            }
            else {
                optGroup.innerHTML = '';
            }
        }
        let roomId = parseInt(document.getElementById('divRoomSelector').getAttribute('data-roomid'), 10)

        if (typeof groups !== 'undefined') {
            groups.sort((a, b) => { return a.sortOrder - b.sortOrder });


            for (let i = 0; i < groups.length; i++) {
                let group = groups[i];
                let room = _rooms.find(x => x.roomId === group.roomId) || { roomId: 0, name: '' };

                divCfg += `<div class="somfyGroup group-draggable" draggable="true" data-roomid="${group.roomId}" data-groupid="${group.groupId}" data-remoteaddress="${group.remoteAddress}">`;
                divCfg += `<div class="button-outline" onclick="somfy.openEditGroup(${group.groupId});"><i class="icss-edit"></i></div>`;
                //divCfg += `<i class="Group-icon" data-position="${Group.position || 0}%"></i>`;
                divCfg += '<div class="group-name">';
                divCfg += `<div class="cfg-room">${room.name}</div>`;
                divCfg += `<div class="">${group.name}</div>`;
                divCfg += '</div>'
                divCfg += `<span class="group-address">${group.remoteAddress}</span>`;
                divCfg += `<div class="button-outline" onclick="somfy.deleteGroup(${group.groupId});"><i class="icss-trash"></i></div>`;
                divCfg += '</div>';

                divCtl += `<div class="somfyGroupCtl" style="${roomId === 0 || roomId === room.roomId ? '' : 'display:none'}" data-groupId="${group.groupId}" data-roomid="${group.roomId}" data-remoteaddress="${group.remoteAddress}">`;
                divCtl += `<div class="group-name">`;
                divCtl += `<span class="groupctl-room">${room.name}</span>`;
                divCtl += `<span class="groupctl-name">${group.name}</span>`;
                divCtl += `<div class="groupctl-shades">`;
                if (typeof group.linkedShades !== 'undefined') {
                    divCtl += `<label>Members:</label><span>${group.linkedShades.length}`;
                    /*
                    for (let j = 0; j < group.linkedShades.length; j++) {
                        divCtl += '<span>';
                        if (j !== 0) divCtl += ', ';
                        divCtl += group.linkedShades[j].name;
                        divCtl += '</span>';
                    }
                    */
                }
                divCtl += '</div></div>';
                divCtl += `<div class="groupctl-buttons">`;
                divCtl += `<div class="button-sunflag cmd-button" data-cmd="sunflag" data-groupid="${group.groupId}" data-on="${group.flags & 0x20 ? 'true' : 'false'}" style="${!group.sunSensor ? 'display:none' : ''}"><i class="icss-sun-c"></i><i class="icss-sun-o"></i></div>`;
                divCtl += `<div class="button-outline cmd-button" data-cmd="up" data-groupid="${group.groupId}"><i class="icss-somfy-up"></i></div>`;
                divCtl += `<div class="button-outline cmd-button my-button" data-cmd="my" data-groupid="${group.groupId}" style="font-size:2em;padding:10px;"><span>my</span></div>`;
                divCtl += `<div class="button-outline cmd-button" data-cmd="down" data-groupid="${group.groupId}"><i class="icss-somfy-down" style="margin-top:-4px;"></i></div>`;
                divCtl += '</div></div>';
                let opt = document.createElement('option');
                opt.innerHTML = group.name;
                opt.setAttribute('data-address', group.remoteAddress);
                opt.setAttribute('data-type', 'group');
                opt.setAttribute('data-groupid', group.groupId);
                opt.setAttribute('data-bitlength', group.bitLength);
                optGroup.appendChild(opt);
            }
        }
        let sopt = vrList.options[vrList.selectedIndex];
        document.getElementById('divVirtualRemote').setAttribute('data-bitlength', sopt ? sopt.getAttribute('data-bitlength') : 'none');

        document.getElementById('divGroupList').innerHTML = divCfg;
        let groupControls = document.getElementById('divGroupControls');
        groupControls.innerHTML = divCtl;
        // Attach the timer for setting the My Position for the Group.
        let btns = groupControls.querySelectorAll('div.cmd-button');
        for (let i = 0; i < btns.length; i++) {
            btns[i].addEventListener('click', (event) => {
                console.log(this);
                console.log(event);
                let groupId = parseInt(event.currentTarget.getAttribute('data-groupid'), 10);
                let cmd = event.currentTarget.getAttribute('data-cmd');
                if (cmd === 'sunflag') {
                    if (makeBool(event.currentTarget.getAttribute('data-on')))
                        this.sendGroupCommand(groupId, 'flag');
                    else
                        this.sendGroupCommand(groupId, 'sunflag');
                }
                else
                    this.sendGroupCommand(groupId, cmd);
            }, true);
        }
        this.setListDraggable(document.getElementById('divGroupList'), '.group-draggable', (list) => {
            // Get the shade order
            let items = list.querySelectorAll('.group-draggable');
            let order = [];
            for (let i = 0; i < items.length; i++) {
                order.push(parseInt(items[i].getAttribute('data-groupid'), 10));
                // Reorder the shades on the main page.
            }
            putJSONSync('/groupSortOrder', order, (err) => {
                for (let i = order.length - 1; i >= 0; i--) {
                    let el = groupControls.querySelector(`.somfyGroupCtl[data-groupid="${order[i]}"`);
                    if (el) {
                        groupControls.prepend(el);
                    }
                }
            });
        });

    }
    closeShadePositioners() {
        let ctls = document.querySelectorAll('.shade-positioner');
        for (let i = 0; i < ctls.length; i++) {
            console.log('Closing shade positioner');
            ctls[i].remove();
        }
    }
    openSetMyPosition(shadeId) {
        if (typeof shadeId === 'undefined') return;
        else {
            let shade = document.querySelector(`div.somfyShadeCtl[data-shadeid="${shadeId}"]`);
            if (shade) {
                this.closeShadePositioners();
                let currPos = parseInt(shade.getAttribute('data-position'), 10);
                let currTiltPos = parseInt(shade.getAttribute('data-tiltposition'), 10);
                let myPos = parseInt(shade.getAttribute('data-mypos'), 10);
                let tiltType = parseInt(shade.getAttribute('data-tilt'), 10);
                let myTiltPos = parseInt(shade.getAttribute('data-mytiltpos'), 10);
                let elname = shade.querySelector(`.shadectl-name`);
                let shadeName = elname.innerHTML;
                let html = `<div class="shade-name"><span>${shadeName}</span><div style="float:right;vertical-align:top;cursor:pointer;font-size:12px;margin-top:4px;">`;
                if (myPos >= 0 && tiltType !== 3)
                    html += `<div onclick="document.getElementById('slidShadeTarget').value = ${myPos}; document.getElementById('slidShadeTarget').dispatchEvent(new Event('change'));"><span style="display:inline-block;width:47px;">Current:</span><span>${myPos}</span><span>%</span></div>`;
                if (myTiltPos >= 0 && tiltType > 0)
                    html += `<div onclick="document.getElementById('slidShadeTiltTarget').value = ${myTiltPos}; document.getElementById('slidShadeTarget').dispatchEvent(new Event('change'));"><span style="display:inline-block;width:47px;">Tilt:</span><span>${myTiltPos}</span><span>%</span></div>`;
                html += `</div></div>`;
                html += `<div id="divShadeTarget">`
                html += `<input id="slidShadeTarget" name="shadeTarget" type="range" min="0" max="100" step="1" oninput="document.getElementById('spanShadeTarget').innerHTML = this.value;" />`;
                html += `<label for="slidShadeTarget"><span>Target Position </span><span><span id="spanShadeTarget" class="shade-target">${currPos}</span><span>%</span></span></label>`;
                html += `</div>`
                html += '<div id="divTiltTarget" style="display:none;">';
                html += `<input id="slidShadeTiltTarget" name="shadeTiltTarget" type="range" min="0" max="100" step="1" oninput="document.getElementById('spanShadeTiltTarget').innerHTML = this.value;" />`;
                html += `<label for="slidShadeTiltTarget"><span>Target Tilt </span><span><span id="spanShadeTiltTarget" class="shade-target">${currTiltPos}</span><span>%</span></span></label>`;
                html += '</div>';
                html += `<hr></hr>`;
                html += '<div style="text-align:right;width:100%;">';
                html += `<button id="btnSetMyPosition" type="button" style="width:auto;display:inline-block;padding-left:10px;padding-right:10px;margin-top:0px;margin-bottom:10px;margin-right:7px;">Set My Position</button>`;
                html += `<button id="btnCancel" type="button" onclick="somfy.closeShadePositioners();" style="width:auto;display:inline-block;padding-left:10px;padding-right:10px;margin-top:0px;margin-bottom:10px;">Cancel</button>`;
                html += `</div></div>`;
                let div = document.createElement('div');
                div.setAttribute('class', 'shade-positioner shade-my-positioner');
                div.setAttribute('data-shadeid', shadeId);
                div.style.height = 'auto';
                div.innerHTML = html;
                shade.appendChild(div);
                let elTarget = div.querySelector('input#slidShadeTarget');
                let elTiltTarget = div.querySelector('input#slidShadeTiltTarget');
                elTarget.value = currPos;
                elTiltTarget.value = currTiltPos;
                let elBtn = div.querySelector('button#btnSetMyPosition');
                if (tiltType === 3) {
                    div.querySelector('div#divTiltTarget').style.display = '';
                    div.querySelector('div#divShadeTarget').style.display = 'none';
                }
                else if (tiltType > 0) div.querySelector('div#divTiltTarget').style.display = '';
                let fnProcessChange = () => {
                    let pos = parseInt(elTarget.value, 10);
                    let tilt = parseInt(elTiltTarget.value, 10);
                    if (tiltType === 3 && tilt === myTiltPos) {
                        elBtn.innerHTML = 'Clear My Position';
                        elBtn.style.background = 'orangered';
                    }
                    else if (pos === myPos && (tiltType === 0 || tilt === myTiltPos)) {
                        elBtn.innerHTML = 'Clear My Position';
                        elBtn.style.background = 'orangered';
                    }
                    else {
                        elBtn.innerHTML = 'Set My Position';
                        elBtn.style.background = '';
                    }
                    document.getElementById('spanShadeTiltTarget').innerHTML = tilt;
                    document.getElementById('spanShadeTarget').innerHTML = pos;
                };
                let fnSetMyPosition = () => {
                    let pos = parseInt(elTarget.value, 10);
                    let tilt = parseInt(elTiltTarget.value, 10);
                    somfy.sendShadeMyPosition(shadeId, pos, tilt);
                };
                fnProcessChange();
                elTarget.addEventListener('change', (event) => { fnProcessChange(); });
                elTiltTarget.addEventListener('change', (event) => { fnProcessChange(); });
                elBtn.addEventListener('click', (event) => { fnSetMyPosition(); });

            }
        }
    }
    sendShadeMyPosition(shadeId, pos, tilt) {
        console.log(`Sending My Position for shade id ${shadeId} to ${pos} and ${tilt}`);
        let overlay = ui.waitMessage(document.getElementById('divContainer'));
        putJSON('/setMyPosition', { shadeId: shadeId, pos: pos, tilt: tilt }, (err, response) => {
            this.closeShadePositioners();
            overlay.remove();
            console.log(response);
        });
    }
    setLinkedRemotesList(shade) {
        let divCfg = '';
        for (let i = 0; i < shade.linkedRemotes.length; i++) {
            let remote = shade.linkedRemotes[i];
            divCfg += `<div class="somfyLinkedRemote" data-shadeid="${shade.shadeId}" data-remoteaddress="${remote.remoteAddress}" style="text-align:center;">`;
            divCfg += `<span class="linkedremote-address" style="display:inline-block;width:127px;text-align:left;">${remote.remoteAddress}</span>`;
            divCfg += `<span class="linkedremote-code" style="display:inline-block;width:77px;text-align:left;">${remote.lastRollingCode}</span>`;
            divCfg += `<div class="button-outline" onclick="somfy.unlinkRemote(${shade.shadeId}, ${remote.remoteAddress});"><i class="icss-trash"></i></div>`;
            divCfg += '</div>';
        }
        document.getElementById('divLinkedRemoteList').innerHTML = divCfg;
    }
    setLinkedShadesList(group) {
        let divCfg = '';
        for (let i = 0; i < group.linkedShades.length; i++) {
            let shade = group.linkedShades[i];
            divCfg += `<div class="linked-shade" data-shadeid="${shade.shadeId}" data-remoteaddress="${shade.remoteAddress}">`;
            divCfg += `<span class="linkedshade-name">${shade.name}</span>`;
            divCfg += `<span class="linkedshade-address">${shade.remoteAddress}</span>`;
            divCfg += `<div class="button-outline" onclick="somfy.unlinkGroupShade(${group.groupId}, ${shade.shadeId});"><i class="icss-trash"></i></div>`;
            divCfg += '</div>';
        }
        document.getElementById('divLinkedShadeList').innerHTML = divCfg;
    }
    pinMaps = [
        { name: '', maxPins: 39, inputs: [0, 1, 6, 7, 8, 9, 10, 11, 37, 38], outputs: [3, 6, 7, 8, 9, 10, 11, 34, 35, 36, 37, 38, 39] },
        { name: 's2', maxPins: 46, inputs: [0, 19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 45], outputs: [0, 19, 20, 26, 27, 28, 29, 30, 31, 32, 45, 46]},
        { name: 's3', maxPins: 48, inputs: [19, 20, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32], outputs: [19, 20, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32] },
        { name: 'c3', maxPins: 21, inputs: [11, 12, 13, 14, 15, 16, 17, 18, 19, 20], outputs: [11, 12, 13, 14, 15, 16, 17, 21] }
    ];

    loadPins(type, sel, opt) {
        while (sel.firstChild) sel.removeChild(sel.firstChild);
        let cm = document.getElementById('divContainer').getAttribute('data-chipmodel');
        let pm = this.pinMaps.find(x => x.name === cm) || { name: '', maxPins: 39, inputs: [0, 1, 6, 7, 8, 9, 10, 11, 37, 38], outputs: [3, 6, 7, 8, 9, 10, 11, 34, 35, 36, 37, 38, 39] };
        //console.log({ cm: cm, pm: pm });
        for (let i = 0; i <= pm.maxPins; i++) {
            if (type.includes('in') && pm.inputs.includes(i)) continue;
            if (type.includes('out') && pm.outputs.includes(i)) continue;
            sel.options[sel.options.length] = new Option(`GPIO-${i > 9 ? i.toString() : '0' + i.toString()}`, i, typeof opt !== 'undefined' && opt === i);
        }
    }
    procGroupState(state) {
        console.log(state);
        let flags = document.querySelectorAll(`.button-sunflag[data-groupid="${state.groupId}"]`);
        for (let i = 0; i < flags.length; i++) {
            flags[i].style.display = state.sunSensor ? '' : 'none';
            flags[i].setAttribute('data-on', state.flags & 0x20 === 0x20 ? 'true' : 'false');
        }
    }
    procShadeState(state) {
        console.log(state);
        let icons = document.querySelectorAll(`.somfy-shade-icon[data-shadeid="${state.shadeId}"]`);
        for (let i = 0; i < icons.length; i++) {
            icons[i].style.setProperty('--shade-position', `${state.flipPosition ? 100 - state.position : state.position}%`);
            icons[i].style.setProperty('--fpos', `${state.position}%`);
            //icons[i].style.setProperty('--shade-position', `${state.position}%`);
        }
        if (state.tiltType !== 0) {
            let tilts = document.querySelectorAll(`.icss-window-tilt[data-shadeid="${state.shadeId}"]`);
            for (let i = 0; i < tilts.length; i++) {
                tilts[i].setAttribute('data-tiltposition', `${state.tiltPosition}`);
            }
        }
        let flags = document.querySelectorAll(`.button-sunflag[data-shadeid="${state.shadeId}"]`);
        for (let i = 0; i < flags.length; i++) {
            flags[i].style.display = state.sunSensor ? '' : 'none';
            flags[i].setAttribute('data-on', state.flags & 0x01 === 0x01 ? 'true' : 'false');

        }
        let divs = document.querySelectorAll(`.somfyShadeCtl[data-shadeid="${state.shadeId}"]`);
        for (let i = 0; i < divs.length; i++) {
            divs[i].setAttribute('data-direction', state.direction);
            divs[i].setAttribute('data-position', state.position);
            divs[i].setAttribute('data-target', state.target);
            divs[i].setAttribute('data-mypos', state.myPos);
            divs[i].setAttribute('data-windy', (state.flags & 0x10) === 0x10 ? 'true' : 'false');
            divs[i].setAttribute('data-sunny', (state.flags & 0x20) === 0x20 ? 'true' : 'false');
            if (typeof state.myTiltPos !== 'undefined') divs[i].setAttribute('data-mytiltpos', state.myTiltPos);
            else divs[i].setAttribute('data-mytiltpos', -1);
            if (state.tiltType !== 0) {
                divs[i].setAttribute('data-tiltdirection', state.tiltDirection);
                divs[i].setAttribute('data-tiltposition', state.tiltPosition);
                divs[i].setAttribute('data-tilttarget', state.tiltTarget);
            }
            let span = divs[i].querySelector('span.my-pos');
            if (span) span.innerHTML = typeof state.myPos !== 'undefined' && state.myPos >= 0 ? `${state.myPos}%` : '---';
            span = divs[i].querySelector('span.my-pos-tilt');
            if (span) span.innerHTML = typeof state.myTiltPos !== 'undefined' && state.myTiltPos >= 0 ? `${state.myTiltPos}%` : '---';
        }
    }
    procRemoteFrame(frame) {
        //console.log(frame);
        document.getElementById('spanRssi').innerHTML = frame.rssi;
        document.getElementById('spanFrameCount').innerHTML = parseInt(document.getElementById('spanFrameCount').innerHTML, 10) + 1;
        let lnk = document.getElementById('divLinking');
        if (lnk) {
            let obj = {
                shadeId: parseInt(lnk.dataset.shadeid, 10),
                remoteAddress: frame.address,
                rollingCode: frame.rcode
            };
            let overlay = ui.waitMessage(document.getElementById('divLinking'));
            putJSON('/linkRemote', obj, (err, shade) => {
                console.log(shade);
                overlay.remove();
                lnk.remove();
                this.setLinkedRemotesList(shade);
            });
        }
        else {
            lnk = document.getElementById('divLinkRepeater');
            if (lnk) {
                putJSONSync(`/linkRepeater`, {address:frame.address}, (err, repeaters) => {
                    lnk.remove();
                    if (err) ui.serviceError(err);
                    else this.setRepeaterList(repeaters);
                });
            }
        }
        let frames = document.getElementById('divFrames');
        let row = document.createElement('div');
        row.classList.add('frame-row');
        row.setAttribute('data-valid', frame.valid);
        // The socket is not sending the current date so we will snag the current receive date from
        // the browser.
        let fnFmtDate = (dt) => {
            return `${(dt.getMonth() + 1).fmt('00')}/${dt.getDate().fmt('00')} ${dt.getHours().fmt('00')}:${dt.getMinutes().fmt('00')}:${dt.getSeconds().fmt('00')}.${dt.getMilliseconds().fmt('000')}`;
        };
        let fnFmtTime = (dt) => {
            return `${dt.getHours().fmt('00')}:${dt.getMinutes().fmt('00')}:${dt.getSeconds().fmt('00')}.${dt.getMilliseconds().fmt('000')}`;
        };
        frame.time = new Date();
        let proto = '-S';
        switch (frame.proto) {
            case 1:
                proto = '-W';
                break;
            case 2:
                proto = '-V';
                break;
        }
        let html = `<span>${frame.encKey}</span><span>${frame.address}</span><span>${frame.command}<sup>${frame.stepSize ? frame.stepSize : ''}</sup></span><span>${frame.rcode}</span><span>${frame.rssi}dBm</span><span>${frame.bits}${proto}</span><span>${fnFmtTime(frame.time)}</span><div class="frame-pulses">`;
        for (let i = 0; i < frame.pulses.length; i++) {
            if (i !== 0) html += ',';
            html += `${frame.pulses[i]}`;
        }
        html += '</div>';
        row.innerHTML = html;
        frames.prepend(row);
        this.frames.push(frame);
    }
    JSONPretty(obj, indent = 2) {
        if (Array.isArray(obj)) {
            let output = '[';
            for (let i = 0; i < obj.length; i++) {
                if (i !== 0) output += ',\n';
                output += this.JSONPretty(obj[i], indent);
            }
            output += ']';
            return output;
        }
        else {
            let output = JSON.stringify(obj, function (k, v) {
                if (Array.isArray(v)) return JSON.stringify(v);
                return v;
            }, indent).replace(/\\/g, '')
                .replace(/\"\[/g, '[')
                .replace(/\]\"/g, ']')
                .replace(/\"\{/g, '{')
                .replace(/\}\"/g, '}')
                .replace(/\{\n\s+/g, '{');
            return output;
        }
    }
    framesToClipboard() {
        if (typeof navigator.clipboard !== 'undefined')
            navigator.clipboard.writeText(this.JSONPretty(this.frames, 2));
        else {
            let dummy = document.createElement('textarea');
            document.body.appendChild(dummy);
            dummy.value = this.JSONPretty(this.frames, 2);
            dummy.focus();
            dummy.select();
            document.execCommand('copy');
            document.body.removeChild(dummy);
        }
    }
    onShadeTypeChanged(el) {
        let sel = document.getElementById('selShadeType');
        let tilt = parseInt(document.getElementById('selTiltType').value, 10);
        let ico = document.getElementById('icoShade');
        let type = parseInt(sel.value, 10);
        document.getElementById('somfyShade').setAttribute('data-shadetype', type);
        document.getElementById('divSomfyButtons').setAttribute('data-shadetype', type);
        
        let st = this.shadeTypes.find(x => x.type === type) || { type: type };
        for (let i = 0; i < this.shadeTypes.length; i++) {
            let t = this.shadeTypes[i];
            if (t.type !== type) {
                if (ico.classList.contains(t.ico) && t.ico !== st.ico) ico.classList.remove(t.ico);
            }
            else {
                if (!ico.classList.contains(st.ico)) ico.classList.add(st.ico);
                document.getElementById('divTiltSettings').style.display = st.tilt !== false ? '' : 'none';
                let lift = st.lift || false;
                if (lift && tilt == 3) lift = false;
                if (!st.tilt) tilt = 0;
                document.getElementById('fldTiltTime').parentElement.style.display = tilt ? 'inline-block' : 'none';
                document.getElementById('divLiftSettings').style.display = lift ? '' : 'none';
                document.getElementById('divTiltSettings').style.display = st.tilt ? '' : 'none';
                document.querySelector('#divSomfyButtons i.icss-window-tilt').style.display = tilt ? '' : 'none';
                document.getElementById('divSunSensor').style.display = st.sun ? '' : 'none';
                document.getElementById('divLightSwitch').style.display = st.light ? '' : 'none';
                document.getElementById('divFlipPosition').style.display = st.fpos ? '' : 'none';
                document.getElementById('divFlipCommands').style.display = st.fcmd ? '' : 'none';
                if (!st.light) document.getElementById('cbHasLight').checked = false;
                if (!st.sun) document.getElementById('cbHasSunsensor').checked = false;
            }
        }
    }
    onShadeBitLengthChanged(el) {
        document.getElementById('somfyShade').setAttribute('data-bitlength', el.value);
        //document.getElementById('divStepSettings').style.display = parseInt(el.value, 10) === 80 ? '' : 'none';
    }
    onShadeProtoChanged(el) {
        document.getElementById('somfyShade').setAttribute('data-proto', el.value);
    }
    openEditRoom(roomId) {
        
        if (typeof roomId === 'undefined') {
            document.getElementById('btnSaveRoom').innerText = 'Add Room';
            getJSONSync('/getNextRoom', (err, room) => {
                document.getElementById('spanRoomId').innerText = '*';
                if (err) ui.serviceError(err);
                else {
                    console.log(room);
                    let elRoom = document.getElementById('somfyRoom');
                    room.name = '';
                    ui.toElement(elRoom, room);
                    this.showEditRoom(true);
                }
            });
        }
        else {
            document.getElementById('btnSaveRoom').innerText = 'Save Room';
            getJSONSync(`/room?roomId=${roomId}`, (err, room) => {
                if (err) ui.serviceError(err);
                else {
                    console.log(room);
                    document.getElementById('spanRoomId').innerText = roomId;
                    ui.toElement(document.getElementById('somfyRoom'), room);
                    this.showEditRoom(true);
                    document.getElementById('btnSaveRoom').style.display = 'inline-block';
                }
            });

        }
    }
    openEditShade(shadeId) {
        if (typeof shadeId === 'undefined') {
            getJSONSync('/getNextShade', (err, shade) => {
                document.getElementById('btnPairShade').style.display = 'none';
                document.getElementById('btnUnpairShade').style.display = 'none';
                document.getElementById('btnLinkRemote').style.display = 'none';
                document.getElementById('btnSaveShade').innerText = 'Add Shade';
                document.getElementById('spanShadeId').innerText = '*';
                document.getElementById('divLinkedRemoteList').innerHTML = '';
                document.getElementById('btnSetRollingCode').style.display = 'none';
                //document.getElementById('selShadeBitLength').value = 56;
                //document.getElementById('cbFlipCommands').value = false;
                //document.getElementById('cbFlipPosition').value = false;
                if (err) {
                    ui.serviceError(err);
                }
                else {
                    console.log(shade);
                    let elShade = document.getElementById('somfyShade');
                    shade.name = '';
                    shade.downTime = shade.upTime = 10000;
                    shade.tiltTime = 7000;
                    shade.bitLength = 56;
                    shade.flipCommands = shade.flipPosition = false;
                    ui.toElement(elShade, shade);
                    this.showEditShade(true);
                    elShade.setAttribute('data-bitlength', shade.bitLength);
                }
            });
        }
        else {
            // Load up an exist shade.
            document.getElementById('btnSaveShade').style.display = 'none';
            document.getElementById('btnPairShade').style.display = 'none';
            document.getElementById('btnUnpairShade').style.display = 'none';
            document.getElementById('btnLinkRemote').style.display = 'none';

            document.getElementById('btnSaveShade').innerText = 'Save Shade';
            document.getElementById('spanShadeId').innerText = shadeId;
            getJSONSync(`/shade?shadeId=${shadeId}`, (err, shade) => {
                if (err) {
                    ui.serviceError(err);
                }
                else {
                    console.log(shade);
                    ui.toElement(document.getElementById('somfyShade'), shade);
                    this.showEditShade(true);
                    document.getElementById('btnSaveShade').style.display = 'inline-block';
                    document.getElementById('btnLinkRemote').style.display = '';
                    this.onShadeTypeChanged(document.getElementById('selShadeType'));
                    let ico = document.getElementById('icoShade');
                    let tilt = ico.parentElement.querySelector('i.icss-window-tilt');
                    tilt.style.display = shade.tiltType !== 0 ? '' : 'none';
                    tilt.setAttribute('data-tiltposition', shade.tiltPosition);
                    tilt.setAttribute('data-shadeid', shade.shadeId);
                    ico.style.setProperty('--shade-position', `${shade.flipPosition ? 100 - shade.position : shade.position}%`);
                    ico.style.setProperty('--fpos', `${shade.position}%`);
                    
                    ico.style.setProperty('--tilt-position', `${shade.flipPosition ? 100 - shade.tiltPosition : shade.tiltPosition}%`);
                    //ico.style.setProperty('--shade-position', `${shade.position}%`);
                    //ico.style.setProperty('--tilt-position', `${shade.tiltPosition}%`);

                    ico.setAttribute('data-shadeid', shade.shadeId);
                    somfy.onShadeBitLengthChanged(document.getElementById('selShadeBitLength'));
                    somfy.onShadeProtoChanged(document.getElementById('selShadeProto'));
                    document.getElementById('btnSetRollingCode').style.display = 'inline-block';
                    if (shade.paired) {
                        document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    }
                    else {
                        document.getElementById('btnPairShade').style.display = 'inline-block';
                    }
                    this.setLinkedRemotesList(shade);
                }
            });
        }
    }
    openEditGroup(groupId) {
        document.getElementById('btnLinkShade').style.display = 'none';
        if (typeof groupId === 'undefined') {
            getJSONSync('/getNextGroup', (err, group) => {
                document.getElementById('btnSaveGroup').innerText = 'Add Group';
                document.getElementById('spanGroupId').innerText = '*';
                document.getElementById('divLinkedShadeList').innerHTML = '';
                //document.getElementById('btnSetRollingCode').style.display = 'none';
                if (err) {
                    ui.serviceError(err);
                }
                else {
                    console.log(group);
                    group.name = '';
                    group.flipCommands = false;
                    ui.toElement(document.getElementById('somfyGroup'), group);
                    this.showEditGroup(true);
                }
            });
        }
        else {
            // Load up an existing group.
            document.getElementById('btnSaveGroup').style.display = 'none';
            document.getElementById('btnSaveGroup').innerText = 'Save Group';
            document.getElementById('spanGroupId').innerText = groupId;
            getJSONSync(`/group?groupId=${groupId}`, (err, group) => {
                if (err) {
                    ui.serviceError(err);
                }
                else {
                    console.log(group);
                    ui.toElement(document.getElementById('somfyGroup'), group);
                    this.showEditGroup(true);
                    document.getElementById('btnSaveGroup').style.display = 'inline-block';
                    document.getElementById('btnLinkShade').style.display = '';
                    document.getElementById('btnSetRollingCode').style.display = 'inline-block';
                    this.setLinkedShadesList(group);
                }
            });
        }
    }
    showEditRoom(bShow) {
        let el = document.getElementById('divLinking');
        if (el) el.remove();
        el = document.getElementById('divLinkRepeater');
        if (el) el.remove();
        el = document.getElementById('divPairing');
        if (el) el.remove();
        el = document.getElementById('divRollingCode');
        if (el) el.remove();
        el = document.getElementById('somfyRoom');
        if (el) el.style.display = bShow ? '' : 'none';
        el = document.getElementById('divRoomListContainer');
        if (el) el.style.display = bShow ? 'none' : '';
        if (bShow) {
            this.showEditGroup(false);
            this.showEditShade(false);
        }
    }
    showEditShade(bShow) {
        let el = document.getElementById('divLinking');
        if (el) el.remove();
        el = document.getElementById('divLinkRepeater');
        if (el) el.remove();
        el = document.getElementById('divPairing');
        if (el) el.remove();
        el = document.getElementById('divRollingCode');
        if (el) el.remove();
        el = document.getElementById('somfyShade');
        if (el) el.style.display = bShow ? '' : 'none';
        el = document.getElementById('divShadeListContainer');
        if (el) el.style.display = bShow ? 'none' : '';
        if (bShow) {
            this.showEditGroup(false);
            this.showEditRoom(false);
        }
    }
    showEditGroup(bShow) {
        let el = document.getElementById('divLinking');
        if (el) el.remove();
        el = document.getElementById('divLinkRepeater');
        if (el) el.remove();
        el = document.getElementById('divPairing');
        if (el) el.remove();
        el = document.getElementById('divRollingCode');
        if (el) el.remove();
        el = document.getElementById('somfyGroup');
        if (el) el.style.display = bShow ? '' : 'none';
        el = document.getElementById('divGroupListContainer');
        if (el) el.style.display = bShow ? 'none' : '';
        if (bShow) {
            this.showEditRoom(false);
            this.showEditShade(false);
        }

    }
    saveRoom() {
        let roomId = parseInt(document.getElementById('spanRoomId').innerText, 10);
        let obj = ui.fromElement(document.getElementById('somfyRoom'));
        let valid = true;
        if (valid && (typeof obj.name !== 'string' || obj.name === '' || obj.name.length > 20)) {
            ui.errorMessage(document.getElementById('divSomfySettings'), 'You must provide a name for the room between 1 and 20 characters.');
            valid = false;
        }
        if (valid) {
            if (isNaN(roomId) || roomId === 0) {
                // We are adding.
                putJSONSync('/addRoom', obj, (err, room) => {
                    if (err) {
                        ui.serviceError(err);
                        console.log(err);
                    }
                    else {
                        console.log(room);
                        document.getElementById('spanRoomId').innerText = room.roomId;
                        document.getElementById('btnSaveRoom').innerText = 'Save Room';
                        document.getElementById('btnSaveRoom').style.display = 'inline-block';
                        this.updateRoomsList();
                    }
                });
            }
            else {
                obj.roomId = roomId;
                putJSONSync('/saveRoom', obj, (err, room) => {
                    if (err) ui.serviceError(err);
                    else this.updateRoomsList();
                    console.log(room);
                });
            }
        }
    }
    saveShade() {
        let shadeId = parseInt(document.getElementById('spanShadeId').innerText, 10);
        let obj = ui.fromElement(document.getElementById('somfyShade'));
        let valid = true;
        if (valid && (isNaN(obj.remoteAddress) || obj.remoteAddress < 1 || obj.remoteAddress > 16777215)) {
            ui.errorMessage(document.getElementById('divSomfySettings'), 'The remote address must be a number between 1 and 16777215.  This number must be unique for all shades.');
            valid = false;
        }
        if (valid && (typeof obj.name !== 'string' || obj.name === '' || obj.name.length > 20)) {
            ui.errorMessage(document.getElementById('divSomfySettings'), 'You must provide a name for the shade between 1 and 20 characters.');
            valid = false;
        }
        if (valid && (isNaN(obj.upTime) || obj.upTime < 1 || obj.upTime > 4294967295)) {
            ui.errorMessage(document.getElementById('divSomfySettings'), 'Up Time must be a value between 0 and 4,294,967,295 milliseconds.  This is the travel time to go from full closed to full open.');
            valid = false;
        }
        if (valid && (isNaN(obj.downTime) || obj.downTime < 1 || obj.downTime > 4294967295)) {
            ui.errorMessage(document.getElementById('divSomfySettings'), 'Down Time must be a value between 0 and 4,294,967,295 milliseconds.  This is the travel time to go from full open to full closed.');
            valid = false;
        }
        if (obj.proto === 8 || obj.proto === 9) {
            switch (obj.shadeType) {
                case 5: // Garage 1-button
                case 14: // Gate left 1-button
                case 15: // Gate center 1-button
                case 16: // Gate right 1-button
                case 10: // Two button dry contact
                    if (obj.proto !== 9 && obj.gpioUp === obj.gpioDown) {
                        ui.errorMessage(document.getElementById('divSomfySettings'), 'For GPIO controlled motors the up and down GPIO selections must be unique.');
                        valid = false;
                    }
                    break;
                case 9: // Dry contact.
                    break;
                default:
                    if (obj.gpioUp === obj.gpioDown) {
                        ui.errorMessage(document.getElementById('divSomfySettings'), 'For GPIO controlled motors the up and down GPIO selections must be unique.');
                        valid = false;
                    }
                    else if (obj.proto === 9 && (obj.gpioMy === obj.gpioUp || obj.gpioMy === obj.gpioDown)) {
                        ui.errorMessage(document.getElementById('divSomfySettings'), 'For GPIO controlled motors the up and down and my GPIO selections must be unique.');
                        valid = false;
                    }
                    break;
            }
        }
        if (valid) {
            if (isNaN(shadeId) || shadeId >= 255) {
                // We are adding.
                putJSONSync('/addShade', obj, (err, shade) => {
                    if (err) {
                        ui.serviceError(err);
                        console.log(err);
                    }
                    else {
                        console.log(shade);
                        document.getElementById('spanShadeId').innerText = shade.shadeId;
                        document.getElementById('btnSaveShade').innerText = 'Save Shade';
                        document.getElementById('btnSaveShade').style.display = 'inline-block';
                        document.getElementById('btnLinkRemote').style.display = '';
                        document.getElementById(shade.paired ? 'btnUnpairShade' : 'btnPairShade').style.display = 'inline-block';
                        document.getElementById('btnSetRollingCode').style.display = 'inline-block';
                        this.updateShadeList();
                    }
                });
            }
            else {
                obj.shadeId = shadeId;
                putJSONSync('/saveShade', obj, (err, shade) => {
                    if (err) ui.serviceError(err);
                    else this.updateShadeList();
                    console.log(shade);
                    let ico = document.getElementById('icoShade');
                    let tilt = ico.parentElement.querySelector('i.icss-window-tilt');
                    tilt.style.display = shade.tiltType !== 0 ? '' : 'none';
                    tilt.setAttribute('data-tiltposition', shade.tiltPosition);
                    tilt.setAttribute('data-shadeid', shade.shadeId);
                    ico.style.setProperty('--shade-position', `${shade.flipPosition ? 100 - shade.position : shade.position}%`);
                    ico.style.setProperty('--fpos', `${shade.position}%`);

                    ico.style.setProperty('--tilt-position', `${shade.flipPosition ? 100 - shade.tiltPosition : shade.tiltPosition}%`);

                });
            }
        }
    }
    saveGroup() {
        let groupId = parseInt(document.getElementById('spanGroupId').innerText, 10);
        let obj = ui.fromElement(document.getElementById('somfyGroup'));
        let valid = true;
        if (valid && (isNaN(obj.remoteAddress) || obj.remoteAddress < 1 || obj.remoteAddress > 16777215)) {
            ui.errorMessage('The remote address must be a number between 1 and 16777215.  This number must be unique for all shades.');
            valid = false;
        }
        if (valid && (typeof obj.name !== 'string' || obj.name === '' || obj.name.length > 20)) {
            ui.errorMessage('You must provide a name for the shade between 1 and 20 characters.');
            valid = false;
        }
        if (valid) {
            if (isNaN(groupId) || groupId >= 255) {
                // We are adding.
                putJSONSync('/addGroup', obj, (err, group) => {
                    if (err) ui.serviceError(err);
                    else {
                        console.log(group);
                        document.getElementById('spanGroupId').innerText = group.groupId;
                        document.getElementById('btnSaveGroup').innerText = 'Save Group';
                        document.getElementById('btnSaveGroup').style.display = 'inline-block';
                        document.getElementById('btnLinkShade').style.display = '';
                        //document.getElementById('btnSetRollingCode').style.display = 'inline-block';
                        this.updateGroupList();
                    }
                });
            }
            else {
                obj.groupId = groupId;
                putJSONSync('/saveGroup', obj, (err, shade) => {
                    if (err) ui.serviceError(err);
                    else this.updateGroupList();
                    console.log(shade);
                });
            }
        }
    }
    updateRoomsList() {
        getJSONSync('/rooms', (err, shades) => {
            if (err) {
                console.log(err);
                ui.serviceError(err);
            }
            else {
                this.setRoomsList(shades);
                
            }
        });
    }
    updateShadeList() {
        getJSONSync('/shades', (err, shades) => {
            if (err) {
                console.log(err);
                ui.serviceError(err);
            }
            else {
                //console.log(shades);
                // Create the shades list.
                this.setShadesList(shades);
            }
        });
    }
    updateGroupList() {
        getJSONSync('/groups', (err, groups) => {
            if (err) {
                console.log(err);
                ui.serviceError(err);
            }
            else {
                console.log(groups);
                // Create the groups list.
                this.setGroupsList(groups);
            }
        });
    }
    updateRepeatList() {
        getJSONSync('/repeaters', (err, repeaters) => {
            if (err) {
                console.log(err);
                ui.serviceError(err);
            }
            else this.setRepeaterList(repeaters);
        });
    }
    deleteRoom(roomId) {
        let valid = true;
        if (isNaN(roomId) || roomId >= 255 || roomId <= 0) {
            ui.errorMessage('A valid room id was not supplied.');
            valid = false;
        }
        if (valid) {
            getJSONSync(`/room?roomId=${roomId}`, (err, room) => {
                if (err) ui.serviceError(err);
                else {
                    let prompt = ui.promptMessage(`Are you sure you want to delete this room?`, () => {
                        ui.clearErrors();
                        putJSONSync('/deleteRoom', { roomId: roomId }, (err, room) => {
                            prompt.remove();
                            if (err) ui.serviceError(err);
                            else
                                this.updateRoomsList();
                        });
                    });
                    prompt.querySelector('.sub-message').innerHTML = `<p>If this room was previously selected for motors or groups, they will be automatically assigned to the Home room.</p>`;
                }
            });
        }
    }
    deleteShade(shadeId) {
        let valid = true;
        if (isNaN(shadeId) || shadeId >= 255 || shadeId <= 0) {
            ui.errorMessage('A valid shade id was not supplied.');
            valid = false;
        }
        if (valid) {
            getJSONSync(`/shade?shadeId=${shadeId}`, (err, shade) => {
                if (err) ui.serviceError(err);
                else if (shade.inGroup) ui.errorMessage(`You may not delete this shade because it is a member of a group.`);
                else {
                    let prompt = ui.promptMessage(`Are you sure you want to delete this shade?`, () => {
                        ui.clearErrors();
                        putJSONSync('/deleteShade', { shadeId: shadeId }, (err, shade) => {
                            this.updateShadeList();
                            prompt.remove;
                        });
                    });
                    prompt.querySelector('.sub-message').innerHTML = `<p>If this shade was previously paired with a motor, you should first unpair it from the motor and remove it from any groups.  Otherwise its address will remain in the motor memory.</p><p>Press YES to delete ${shade.name} or NO to cancel this operation.</p>`;
                }
            });
        }
    }
    deleteGroup(groupId) {
        let valid = true;
        if (isNaN(groupId) || groupId >= 255 || groupId <= 0) {
            ui.errorMessage('A valid shade id was not supplied.');
            valid = false;
        }
        if (valid) {
            getJSONSync(`/group?groupId=${groupId}`, (err, group) => {
                if (err) ui.serviceError(err);
                else {
                    if (group.linkedShades.length > 0) {
                        ui.errorMessage('You may not delete this group until all shades have been removed from it.');
                    }
                    else {
                        let prompt = ui.promptMessage(`Are you sure you want to delete this group?`, () => {
                            putJSONSync('/deleteGroup', { groupId: groupId }, (err, g) => {
                                if (err) ui.serviceError(err);
                                this.updateGroupList();
                                prompt.remove();
                            });

                        });
                        prompt.querySelector('.sub-message').innerHTML = `<p>Press YES to delete the ${group.name} group or NO to cancel this operation.</p>`;
                        
                    }
                }
            });
        }
    }
    sendPairCommand(shadeId) {
        putJSON('/pairShade', { shadeId: shadeId }, (err, shade) => {
            if (err) {
                console.log(err);
            }
            else {
                console.log(shade);
                document.getElementById('somfyMain').style.display = 'none';
                document.getElementById('somfyShade').style.display = '';
                document.getElementById('btnSaveShade').style.display = 'inline-block';
                document.getElementById('btnLinkRemote').style.display = '';
                document.getElementsByName('shadeAddress')[0].value = shade.remoteAddress;
                document.getElementsByName('shadeName')[0].value = shade.name;
                document.getElementsByName('shadeUpTime')[0].value = shade.upTime;
                document.getElementsByName('shadeDownTime')[0].value = shade.downTime;
                let ico = document.getElementById('icoShade');
                ico.style.setProperty('--shade-position', `${shade.flipPosition ? 100 - shade.position : shade.position}%`);
                ico.style.setProperty('--fpos', `${shade.position}%`);
                //ico.style.setProperty('--shade-position', `${shade.position}%`);
                ico.setAttribute('data-shadeid', shade.shadeId);
                if (shade.paired) {
                    document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    document.getElementById('btnPairShade').style.display = 'none';
                }
                else {
                    document.getElementById('btnPairShade').style.display = 'inline-block';
                    document.getElementById('btnUnpairShade').style.display = 'none';
                }
                this.setLinkedRemotesList(shade);
                document.getElementById('divPairing').remove();
            }

        });
    }
    sendUnpairCommand(shadeId) {
        putJSON('/unpairShade', { shadeId: shadeId }, (err, shade) => {
            if (err) {
                console.log(err);
            }
            else {
                console.log(shade);
                document.getElementById('somfyMain').style.display = 'none';
                document.getElementById('somfyShade').style.display = '';
                document.getElementById('btnSaveShade').style.display = 'inline-block';
                document.getElementById('btnLinkRemote').style.display = '';
                document.getElementsByName('shadeAddress')[0].value = shade.remoteAddress;
                document.getElementsByName('shadeName')[0].value = shade.name;
                document.getElementsByName('shadeUpTime')[0].value = shade.upTime;
                document.getElementsByName('shadeDownTime')[0].value = shade.downTime;
                let ico = document.getElementById('icoShade');
                ico.style.setProperty('--shade-position', `${shade.flipPosition ? 100 - shade.position : shade.position}%`);
                ico.style.setProperty('--fpos', `${shade.position}%`);
                //ico.style.setProperty('--shade-position', `${shade.position}%`);
                ico.setAttribute('data-shadeid', shade.shadeId);
                if (shade.paired) {
                    document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    document.getElementById('btnPairShade').style.display = 'none';
                }
                else {
                    document.getElementById('btnPairShade').style.display = 'inline-block';
                    document.getElementById('btnUnpairShade').style.display = 'none';
                }
                this.setLinkedRemotesList(shade);
                document.getElementById('divPairing').remove();
            }
        });
    }
    setRollingCode(shadeId, rollingCode) {
        putJSONSync('/setRollingCode', { shadeId: shadeId, rollingCode: rollingCode }, (err, shade) => {
            if (err) ui.serviceError(document.getElementById('divSomfySettings'), err);
            else {
                let dlg = document.getElementById('divRollingCode');
                if (dlg) dlg.remove();
            }
        });
    }
    openSetRollingCode(shadeId) {
        let overlay = ui.waitMessage(document.getElementById('divContainer'));
        getJSON(`/shade?shadeId=${shadeId}`, (err, shade) => {
            overlay.remove();
            if (err) {
                ui.serviceError(err);
            }
            else {
                console.log(shade);
                let div = document.createElement('div');
                div.setAttribute('id', 'divRollingCode');
                let html = `<div class="instructions" data-shadeid="${shadeId}">`;
                html += '<div style="width:100%;color:red;text-align:center;font-weight:bold;"><span style="background:yellow;padding:10px;display:inline-block;border-radius:5px;background:white;">BEWARE ... WARNING ... DANGER<span></div>';
                html += '<hr style="width:100%;margin:0px;"></hr>';
                html += '<p style="font-size:14px;">If this shade is already paired with a motor then changing the rolling code WILL cause it to stop working.  Rolling codes are tied to the remote address and the Somfy motor expects these to be sequential.</p>';
                html += '<p style="font-size:14px;">If you hesitated just a little bit do not press the red button.  Green represents safety so press it, wipe the sweat from your brow, and go through the normal pairing process.';
                html += '<div class="field-group" style="border-radius:5px;background:white;width:50%;margin-left:25%;text-align:center">';
                html += `<input id="fldNewRollingCode" min="0" max="65535" name="newRollingCode" type="number" length="12" style="text-align:center;font-size:24px;" placeholder="New Code" value="${shade.lastRollingCode}"></input>`;
                html += '<label for="fldNewRollingCode">Rolling Code</label>';
                html += '</div>';
                html += `<div class="button-container">`;
                html += `<button id="btnChangeRollingCode" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;background:orangered;" onclick="somfy.setRollingCode(${shadeId}, parseInt(document.getElementById('fldNewRollingCode').value, 10));">Set Rolling Code</button>`;
                html += `<button id="btnCancel" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;background:lawngreen;color:gray" onclick="document.getElementById('divRollingCode').remove();">Cancel</button>`;
                html += `</div>`;
                div.innerHTML = html;
                document.getElementById('somfyShade').appendChild(div);
            }
        });
    }
    setPaired(shadeId, paired) {
        let obj = { shadeId: shadeId, paired: paired || false };
        let div = document.getElementById('divPairing');
        let overlay = typeof div === 'undefined' ? undefined : ui.waitMessage(div);
        putJSONSync('/setPaired', obj, (err, shade) => {
            if (overlay) overlay.remove();
            if (err) {
                console.log(err);
                ui.errorMessage(err.message);
            }
            else if (div) {
                console.log(shade);
                this.showEditShade(true);
                document.getElementById('btnSaveShade').style.display = 'inline-block';
                document.getElementById('btnLinkRemote').style.display = '';
                if (shade.paired) {
                    document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    document.getElementById('btnPairShade').style.display = 'none';
                }
                else {
                    document.getElementById('btnPairShade').style.display = 'inline-block';
                    document.getElementById('btnUnpairShade').style.display = 'none';
                }
                this.setLinkedRemotesList(shade);
                div.remove();
            }
        });
    }
    pairShade(shadeId) {
        let shadeType = parseInt(document.getElementById('somfyShade').getAttribute('data-shadetype'), 10);
        let div = document.createElement('div');
        let html = `<div id="divPairing" class="instructions" data-type="link-remote" data-shadeid="${shadeId}">`;
        if (shadeType === 5 || shadeType === 6) {
            html += '<div>Follow the instructions below to pair ESPSomfy RTS with an RTS Garage Door motor</div>';
            html += '<hr style="width:100%;margin:0px;"></hr>';
            html += '<ul style="width:100%;margin:0px;padding-left:20px;font-size:14px;">';
            html += '<li>Open the garage door motor memory per instructions for your motor.</li>';
            html += '<li>Once the memory is opened, press the prog button below</li>';
            html += '<li>For single button control ESPSomfy RTS will send a toggle command but for a 3 button control it will send a prog command.</li>';
            html += '</ul>';
            html += `<div class="button-container">`;
            html += `<button id="btnSendPairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;">Prog</button>`;
            html += `<button id="btnMarkPaired" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;" onclick="somfy.setPaired(${shadeId}, true);">Door Paired</button>`;
            html += `<button id="btnStopPairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block" >Close</button>`;
            html += `</div>`;
        }
        else {
            html += '<div>Follow the instructions below to pair this shade with a Somfy motor</div>';
            html += '<hr style="width:100%;margin:0px;"></hr>';
            html += '<ul style="width:100%;margin:0px;padding-left:20px;font-size:14px;">';
            html += '<li>Open the shade memory using an existing remote by pressing the prog button on the back until the shade jogs.</li>';
            html += '<li>After the shade jogs press the Prog button below</li>';
            html += '<li>The shade should jog again indicating that the shade is paired. NOTE: On some motors you may need to press and hold the Prog button.</li>';
            html += '<li>If the shade jogs, you can press the shade paired button.</li>';
            html += '<li>If the shade does not jog, try pressing the prog button again.</li>';
            html += '</ul>';
            html += `<div class="button-container">`;
            html += `<button id="btnSendPairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;">Prog</button>`;
            html += `<button id="btnMarkPaired" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;" onclick="somfy.setPaired(${shadeId}, true);">Shade Paired</button>`;
            html += `<button id="btnStopPairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block" >Close</button>`;
            html += `</div>`;
        }
        let fnRepeatProg = (err, shade) => {
            if (this.btnTimer) {
                clearTimeout(this.btnTimer);
                this.btnTimer = null;
            }
            if (err) return;
            if (mouseDown) {
                somfy.sendCommandRepeat(shadeId, 'prog', null, fnRepeatProg);
            }
        }
        div.innerHTML = html;
        document.getElementById('somfyShade').appendChild(div);
        document.getElementById('btnStopPairing').addEventListener('click', (event) => {
            console.log(this);
            console.log(event);
            console.log('close');
            if (this.btnTimer) {
                clearInterval(this.btnTimer);
                this.btnTimer = null;
            }
            document.getElementById('divPairing').remove();
        });
        let btn = document.getElementById('btnSendPairing');
        btn.addEventListener('mousedown', (event) => {
            console.log(this);
            console.log(event);
            console.log('mousedown');
            somfy.sendCommand(shadeId, 'prog', null, (err, shade) => { fnRepeatProg(err, shade); });
        }, true);
        btn.addEventListener('touchstart', (event) => {
            console.log(this);
            console.log(event);
            console.log('touchstart');
            somfy.sendCommand(shadeId, 'prog', null, (err, shade) => { fnRepeatProg(err, shade); });
        }, true);
        return div;
    }
    unpairShade(shadeId) {
        let div = document.createElement('div');
        let html = `<div id="divPairing" class="instructions" data-type="link-remote" data-shadeid="${shadeId}">`;
        html += '<div>Follow the instructions below to unpair this shade from a Somfy motor</div>';
        html += '<hr style="width:100%;margin:0px;"></hr>';
        html += '<ul style="width:100%;margin:0px;padding-left:20px;font-size:14px;">';
        html += '<li>Open the shade memory using an existing remote</li>';
        html += '<li>Press the prog button on the back of the remote until the shade jogs</li>';
        html += '<li>After the shade jogs press the Prog button below</li>';
        html += '<li>The shade should jog again indicating that the shade is unpaired</li>';
        html += '<li>If the shade jogs, you can press the shade unpaired button.</li>';
        html += '<li>If the shade does not jog, press the prog button again until the shade jogs.</li>';
        html += '</ul>';
        html += `<div class="button-container">`;
        html += `<button id="btnSendUnpairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;">Prog</button>`;
        html += `<button id="btnMarkPaired" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;" onclick="somfy.setPaired(${shadeId}, false);">Shade Unpaired</button>`;
        html += `<button id="btnStopUnpairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block" onclick="document.getElementById('divPairing').remove();">Close</button>`;
        html += `</div>`;
        div.innerHTML = html;
        let fnRepeatProg = (err, shade) => {
            if (this.btnTimer) {
                clearTimeout(this.btnTimer);
                this.btnTimer = null;
            }
            if (err) return;
            if (mouseDown) {
                somfy.sendCommandRepeat(shadeId, 'prog', null, fnRepeatProg);
            }
        }
        document.getElementById('somfyShade').appendChild(div);
        let btn = document.getElementById('btnSendUnpairing');
        btn.addEventListener('mousedown', (event) => {
            console.log(this);
            console.log(event);
            console.log('mousedown');
            somfy.sendCommand(shadeId, 'prog', null, (err, shade) => { fnRepeatProg(err, shade); });
        }, true);
        btn.addEventListener('touchstart', (event) => {
            console.log(this);
            console.log(event);
            console.log('touchstart');
            somfy.sendCommand(shadeId, 'prog', null, (err, shade) => { fnRepeatProg(err, shade); });
        }, true);

        return div;
    }
    sendCommand(shadeId, command, repeat, cb) {
        let obj = {};
        if (typeof shadeId.shadeId !== 'undefined') {
            obj = shadeId;
            cb = command;
            shadeId = obj.shadeId;
            repeat = obj.repeat;
            command = obj.command;
        }
        else {
            obj = { shadeId: shadeId };
            if (isNaN(parseInt(command, 10))) obj.command = command;
            else obj.target = parseInt(command, 10);
            if (typeof repeat === 'number') obj.repeat = parseInt(repeat);
        }
        putJSON('/shadeCommand', obj, (err, shade) => {
            if (typeof cb === 'function') cb(err, shade);
        });
    }
    sendCommandRepeat(shadeId, command, repeat, cb) {
        //console.log(`Sending Shade command ${shadeId}-${command}`);
        let obj = {};
        if (typeof shadeId.shadeId !== 'undefined') {
            obj = shadeId;
            cb = command;
            shadeId = obj.shadeId;
            repeat = obj.repeat;
            command = obj.command;
        }
        else {
            obj = { shadeId: shadeId, command: command };
            if (typeof repeat === 'number') obj.repeat = parseInt(repeat);
        }
        putJSON('/repeatCommand', obj, (err, shade) => {
            if (typeof cb === 'function') cb(err, shade);
        });

        /*
        putJSON(`/repeatCommand?shadeId=${shadeId}&command=${command}`, null, (err, shade) => {
            if(typeof cb === 'function') cb(err, shade);
        });
        */
    }
    sendGroupRepeat(groupId, command, repeat, cb) {
        let obj = { groupId: groupId, command: command };
        if (typeof repeat === 'number') obj.repeat = parseInt(repeat);
        putJSON(`/repeatCommand?groupId=${groupId}&command=${command}`, null, (err, group) => {
            if (typeof cb === 'function') cb(err, group);
        });
    }
    sendVRCommand(el) {
        let pnl = document.getElementById('divVirtualRemote');
        let dd = pnl.querySelector('#selVRMotor');
        let opt = dd.selectedOptions[0];
        let o = {
            type: opt.getAttribute('data-type'),
            address: opt.getAttribute('data-address'),
            cmd: el.getAttribute('data-cmd')
        };
        ui.fromElement(el.parentElement.parentElement, o);
        switch (o.type) {
            case 'shade':
                o.shadeId = parseInt(opt.getAttribute('data-shadeId'), 10);
                o.shadeType = parseInt(opt.getAttribute('data-shadeType'), 10);
                break;
            case 'group':
                o.groupId = parseInt(opt.getAttribute('data-groupId'), 10);
                break;
        }
        console.log(o);
        let fnRepeatCommand = (err, shade) => {
            if (this.btnTimer) {
                clearTimeout(this.btnTimer);
                this.btnTimer = null;
            }
            if (err) return;
            if (mouseDown) {
                if (o.cmd === 'Sensor')
                    somfy.sendSetSensor(o);
                else if (o.type === 'group')
                    somfy.sendGroupRepeat(o.groupId, o.cmd, null, fnRepeatCommand);
                else
                    somfy.sendCommandRepeat(o, fnRepeatCommand);
            }
        }
        o.command = o.cmd;
        if (o.cmd === 'Sensor') {
            somfy.sendSetSensor(o);
        }
        else if (o.type === 'group')
            somfy.sendGroupCommand(o.groupId, o.cmd, null, (err, group) => { fnRepeatCommand(err, group); });
        else
            somfy.sendCommand(o, (err, shade) => { fnRepeatCommand(err, shade); });
    }
    sendSetSensor(obj, cb) {
        putJSON('/setSensor', obj, (err, device) => {
            if (typeof cb === 'function') cb(err, device);
        });
    }
    sendGroupCommand(groupId, command, repeat, cb) {
        console.log(`Sending Group command ${groupId}-${command}`);
        let obj = { groupId: groupId };
        if (isNaN(parseInt(command, 10))) obj.command = command;
        if (typeof repeat === 'number') obj.repeat = parseInt(repeat);
        putJSON('/groupCommand', obj, (err, group) => {
            if (typeof cb === 'function') cb(err, group);
        });
    }
    sendTiltCommand(shadeId, command, cb) {
        console.log(`Sending Tilt command ${shadeId}-${command}`);
        if (isNaN(parseInt(command, 10)))
            putJSON('/tiltCommand', { shadeId: shadeId, command: command }, (err, shade) => {
                if (typeof cb === 'function') cb(err, shade);
            });
        else
            putJSON('/tiltCommand', { shadeId: shadeId, target: parseInt(command, 10) }, (err, shade) => {
                if (typeof cb === 'function') cb(err, shade);
            });
    }
    linkRemote(shadeId) {
        let div = document.createElement('div');
        let html = `<div id="divLinking" class="instructions" data-type="link-remote" data-shadeid="${shadeId}">`;
        html += '<div>Press any button on the remote to link it to this shade.  This will not change the pairing for the remote and this screen will close when the remote is detected.</div>';
        html += '<hr></hr>';
        html += `<div><div class="button-container"><button id="btnStopLinking" type="button" style="padding-left:20px;padding-right:20px;" onclick="document.getElementById('divLinking').remove();">Cancel</button></div>`;
        html += '</div>';
        div.innerHTML = html;
        document.getElementById('somfyShade').appendChild(div);
        return div;
    }
    linkRepeatRemote() {
        let div = document.createElement('div');
        let html = `<div id="divLinkRepeater" class="instructions" data-type="link-repeatremote" style="border-radius:27px;">`;
        html += '<div>Press any button on the remote to repeat its signals.</div>';
        html += '<div class="sub-message">When assigned, ESPSomfy RTS will act as a repeater and repeat any frames for the identified remotes.</div>'
        html += '<div class="sub-message" style="font-size:14px;">Only assign a repeater when ESPSomfy RTS reliably hears a physical remote but the motor does not.  Repeating unnecessary radio signals will degrade radio performance and never assign the same repeater to more than one ESPSomfy RTS device.  You will have created an insidious echo chamber.</div>'

        html += '<div class="sub-message">Once a signal is detected from the remote this window will close and the remote signals will be repeated.</div>'
        html += '<hr></hr>';
        html += `<div><div class="button-container"><button id="btnStopLinking" type="button" style="padding-left:20px;padding-right:20px;" onclick="document.getElementById('divLinkRepeater').remove();">Cancel</button></div>`;
        html += '</div>';
        div.innerHTML = html;
        document.getElementById('divConfigPnl').appendChild(div);
        return div;
    }

    linkGroupShade(groupId) {
        let div = document.createElement('div');
        let html = `<div id="divLinkGroup" class="inst-overlay wizard" data-type="link-shade" data-groupid="${groupId}" data-stepid="1">`;
        html += '<div style="width:100%;text-align:center;font-weight:bold;"><div style="padding:10px;display:inline-block;width:100%;color:#00bcd4;border-radius:5px;border-top-right-radius:17px;border-top-left-radius:17px;background:white;"><div>ADD SHADE TO GROUP</div><div id="divGroupName" style="font-size:14px;"></div></div></div>';

        html += '<div class="wizard-step" data-stepid="1">';
        html += '<p style="font-size:14px;">This wizard will walk you through the steps required to add shades into a group.  Follow all instructions at each step until the shade is added to the group.</p>';
        html += '<p style="font-size:14px;">During this process the shade should jog exactly two times.  The first time indicates that the motor memory has been enabled and the second time adds the group to the motor memory</p>';
        html += '<p style="font-size:14px;">Each shade must be paired individually to the group.  When you are ready to begin pairing your shade to the group press the NEXT button.</p><hr></hr>';
       
        html += '</div>';

        html += '<div class="wizard-step" data-stepid="2">';
        html += '<p style="font-size:14px;">Choose a shade that you would like to include in this group.  Once you have chosen the shade to include in the link press the NEXT button.</p>';
        html += '<p style="font-size:14px;">Only shades that have not already been included in this group are available the dropdown.  Each shade can be included in multiple groups.</p>';
        html += '<hr></hr>';
        html += `<div class="field-group" style="text-align:center;background-color:white;border-radius:5px;">`;
        html += `<select id="selAvailShades" style="font-size:22px;min-width:277px;text-align:center;" data-bind="shadeId" data-datatype="int" onchange="document.getElementById('divWizShadeName').innerHTML = this.options[this.selectedIndex].text;"><options style="color:black;"></options></select><label for="selAvailShades">Select a Shade</label></div >`;
        html += '</div>';

        html += '<div class="wizard-step" data-stepid="3">';
        html += '<p style="font-size:14px;">Now that you have chosen a shade to pair.  Open the memory for the shade by pressing the OPEN MEMORY button.  The shade should jog to indicate the memory has been opened.</p>';
        html += '<p style="font-size:14px;">The motor should jog only once.  If it jogs more than once then you have again closed the memory on the motor. Once the command is sent to the motor you will be asked if the motor successfully jogged.</p><p style="font-size:14px;">If it did then press YES if not press no and click the OPEN MEMORY button again.</p>';
        html += '<hr></hr>';
        html += '<div id="divWizShadeName" style="text-align:center;font-size:22px;"></div>';
        html += '<div class="button-container"><button type="button" id="btnOpenMemory">Open Memory</button></div>';
        html += '<hr></hr>';
        html += '</div>';

        html += '<div class="wizard-step" data-stepid="4">';
        html += '<p style="font-size:14px;">Now that the memory is opened on the motor you need to send the pairing command for the group.</p>';
        html += '<p style="font-size:14px;">To do this press the PAIR TO GROUP button below and once the motor jogs the process will be complete.</p>';
        html += '<hr></hr>';
        html += '<div id="divWizShadeName" style="text-align:center;font-size:22px;"></div>';
        html += '<div class="button-container"><button type="button" id="btnPairToGroup">Pair to Group</button></div>';
        html += '<hr></hr>';
        html += '</div>';



        html += `<div class="button-container" style="text-align:center;"><button id="btnPrevStep" type="button" style="padding-left:20px;padding-right:20px;width:37%;margin-right:10px;display:inline-block;" onclick="ui.wizSetPrevStep(document.getElementById('divLinkGroup'));">Go Back</button><button id="btnNextStep" type="button" style="padding-left:20px;padding-right:20px;width:37%;display:inline-block;" onclick="ui.wizSetNextStep(document.getElementById('divLinkGroup'));">Next</button></div>`;
        html += `<div class="button-container" style="text-align:center;"><button id="btnStopLinking" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;width:calc(100% - 100px);" onclick="document.getElementById('divLinkGroup').remove();">Cancel</button></div>`;
        html += '</div>';
        div.innerHTML = html;
        document.getElementById('divContainer').appendChild(div);
        ui.wizSetStep(div, 1);
        let btnOpenMemory = div.querySelector('#btnOpenMemory');
        btnOpenMemory.addEventListener('click', (evt) => {
            let obj = ui.fromElement(div);
            console.log(obj);
            putJSONSync('/shadeCommand', { shadeId: obj.shadeId, command: 'prog', repeat: 40 }, (err, shade) => {
                if (err) ui.serviceError(err);
                else {
                    let prompt = ui.promptMessage('Confirm Motor Response', () => {
                        ui.wizSetNextStep(document.getElementById('divLinkGroup'));
                        prompt.remove();
                    });
                    prompt.querySelector('.sub-message').innerHTML = `<hr></hr><p>Did the shade jog? If the shade jogged press the YES button if not then press the NO button and try again.</p><p>Once the shade has jogged the motor memory will be ready to add the shade to the group.</p>`;
                }
            });
        });
        let btnPairToGroup = div.querySelector('#btnPairToGroup');
        let fnRepeatProgCommand = (err, o) => {
            console.log(o);
            if (this.btnTimer) {
                clearTimeout(this.btnTimer);
                this.btnTimer = null;
            }
            if (err) return;
            if (mouseDown) {
                if (o.cmd === 'Sensor')
                    somfy.sendSetSensor(o);
                else if (typeof o.groupId !== 'undefined')
                    somfy.sendGroupRepeat(o.groupId, 'prog', null, fnRepeatProgCommand);
                else
                    somfy.sendCommandRepeat(o.shadeId, 'prog', null, fnRepeatProgCommand);
            }
        }
        btnPairToGroup.addEventListener('mousedown', (evt) => {
            mouseDown = true;
            somfy.sendGroupCommand(groupId, 'prog', null, fnRepeatProgCommand);
        });
        btnPairToGroup.addEventListener('mouseup', (evt) => {
            mouseDown = false;
            let obj = ui.fromElement(div);
            let prompt = ui.promptMessage('Confirm Motor Response', () => {
                putJSONSync('/linkToGroup', { groupId: groupId, shadeId: obj.shadeId }, (err, group) => {
                    console.log(group);
                    somfy.setLinkedShadesList(group);
                    this.updateGroupList();
                });
                prompt.remove();
                div.remove();
            });
            prompt.querySelector('.sub-message').innerHTML = `<hr></hr><p>Did the shade jog?  If the shade jogged press the YES button and your shade will be linked to the group.  If it did not press the NO button and try again.</p></p><p>Once the shade has jogged the shade will be added to the group and this process will be finished.</p>`;

        });
        getJSONSync(`/groupOptions?groupId=${groupId}`, (err, options) => {
            if (err) {
                div.remove();
                ui.serviceError(err);
            }
            else {
                console.log(options);
                if (options.availShades.length > 0) {
                    // Add in all the available shades.
                    let selAvail = div.querySelector('#selAvailShades');
                    let grpName = div.querySelector('#divGroupName');
                    if (grpName) grpName.innerHTML = options.name;
                    for (let i = 0; i < options.availShades.length; i++) {
                        let shade = options.availShades[i];
                        selAvail.options.add(new Option(shade.name, shade.shadeId));
                    }
                    let divWizShadeName = div.querySelector('#divWizShadeName');
                    if (divWizShadeName) divWizShadeName.innerHTML = options.availShades[0].name;
                }
                else {
                    div.remove();
                    ui.errorMessage('There are no available shades to pair to this group.');
                }
            }
        });
        return div;
    }
    unlinkGroupShade(groupId, shadeId) {
        let div = document.createElement('div');
        let html = `<div id="divUnlinkGroup" class="inst-overlay wizard" data-type="link-shade" data-groupid="${groupId}" data-stepid="1">`;
        html += '<div style="width:100%;text-align:center;font-weight:bold;"><div style="padding:10px;display:inline-block;width:100%;color:#00bcd4;border-radius:5px;border-top-right-radius:17px;border-top-left-radius:17px;background:white;"><div>REMOVE SHADE FROM GROUP</div><div id="divGroupName" style="font-size:14px;"></div></div></div>';

        html += '<div class="wizard-step" data-stepid="1">';
        html += '<p style="font-size:14px;">This wizard will walk you through the steps required to remove a shade from a group.  Follow all instructions at each step until the shade is removed from the group.</p>';
        html += '<p style="font-size:14px;">During this process the shade should jog exactly two times.  The first time indicates that the motor memory has been enabled and the second time removes the group from the motor memory</p>';
        html += '<p style="font-size:14px;">Each shade must be removed from the group individually.  When you are ready to begin unpairing your shade from the group press the NEXT button to begin.</p><hr></hr>';
        html += '</div>';

        html += '<div class="wizard-step" data-stepid="2">';
        html += '<p style="font-size:14px;">You must first open the memory for the shade by pressing the OPEN MEMORY button.  The shade should jog to indicate the memory has been opened.</p>';
        html += '<p style="font-size:14px;">The motor should jog only once.  If it jogs more than once then you have again closed the memory on the motor. Once the motor has jogged press the NEXT button to proceed.</p>';
        html += '<hr></hr>';
        html += '<div id="divWizShadeName" style="text-align:center;font-size:22px;"></div>';
        html += '<div class="button-container"><button type="button" id="btnOpenMemory">Open Memory</button></div>';
        html += '<hr></hr>';
        html += '</div>';

        html += '<div class="wizard-step" data-stepid="3">';
        html += '<p style="font-size:14px;">Now that the memory is opened on the motor you need to send the un-pairing command for the group.</p>';
        html += '<p style="font-size:14px;">To do this press the UNPAIR FROM GROUP button below and once the motor jogs the process will be complete.</p>';
        html += '<hr></hr>';
        html += '<div id="divWizShadeName" style="text-align:center;font-size:22px;"></div>';
        html += '<div class="button-container"><button type="button" id="btnUnpairFromGroup">Unpair from Group</button></div>';
        html += '<hr></hr>';
        html += '</div>';
        html += `<div class="button-container" style="text-align:center;"><button id="btnPrevStep" type="button" style="padding-left:20px;padding-right:20px;width:37%;margin-right:10px;display:inline-block;" onclick="ui.wizSetPrevStep(document.getElementById('divUnlinkGroup'));">Go Back</button><button id="btnNextStep" type="button" style="padding-left:20px;padding-right:20px;width:37%;display:inline-block;" onclick="ui.wizSetNextStep(document.getElementById('divUnlinkGroup'));">Next</button></div>`;
        html += `<div class="button-container" style="text-align:center;"><button id="btnStopLinking" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;width:calc(100% - 100px);" onclick="document.getElementById('divUnlinkGroup').remove();">Cancel</button></div>`;
        html += '</div>';
        div.innerHTML = html;
        document.getElementById('divContainer').appendChild(div);
        ui.wizSetStep(div, 1);
        let btnOpenMemory = div.querySelector('#btnOpenMemory');
        btnOpenMemory.addEventListener('click', (evt) => {
            let obj = ui.fromElement(div);
            console.log(obj);
            putJSONSync('/shadeCommand', { shadeId: shadeId, command: 'prog', repeat: 40 }, (err, shade) => {
                if (err) ui.serviceError(err);
                else {
                    let prompt = ui.promptMessage('Confirm Motor Response', () => {
                        ui.wizSetNextStep(document.getElementById('divUnlinkGroup'));
                        prompt.remove();
                    });
                    prompt.querySelector('.sub-message').innerHTML = `<hr></hr><p>Did the shade jog? If the shade jogged press the YES button if not then press the NO button and try again.</p><p>If you are having trouble getting the motor to jog on this step you may try to open the memory using a remote.  Most often this is done by selecting the channel, then a long press on the prog button.</p><p>If you opened the memory using the alternate method press the NO button to close this message, then press NEXT button to skip the step.</p>`;
                }
            });
        });
        let btnUnpairFromGroup = div.querySelector('#btnUnpairFromGroup');
        btnUnpairFromGroup.addEventListener('click', (evt) => {
            let obj = ui.fromElement(div);
            putJSONSync('/groupCommand', { groupId: groupId, command: 'prog', repeat: 1 }, (err, shade) => {
                if (err) ui.serviceError(err);
                else {
                    let prompt = ui.promptMessage('Confirm Motor Response', () => {
                        putJSONSync('/unlinkFromGroup', { groupId: groupId, shadeId: shadeId }, (err, group) => {
                            console.log(group);
                            somfy.setLinkedShadesList(group);
                            this.updateGroupList();
                        });
                        prompt.remove();
                        div.remove();
                    });
                    prompt.querySelector('.sub-message').innerHTML = `<hr></hr><p>Did the shade jog? If the shade jogged press the YES button if not then press the NO button and try again.</p><p>Once the shade has jogged the shade will be removed from the group and this process will be finished.</p>`;
                }
            });
        });
        getJSONSync(`/group?groupId=${groupId}`, (err, group) => {
            if (err) {
                div.remove();
                ui.serviceError(err);
            }
            else {
                console.log(group);
                console.log(shadeId);
                let shade = group.linkedShades.find((x) => { return shadeId === x.shadeId; });
                if (typeof shade !== 'undefined') {
                    // Add in all the available shades.
                    let grpName = div.querySelector('#divGroupName');
                    if (grpName) grpName.innerHTML = group.name;
                    let divWizShadeName = div.querySelector('#divWizShadeName');
                    if (divWizShadeName) divWizShadeName.innerHTML = shade.name;
                }
                else {
                    div.remove();
                    ui.errorMessage('The specified shade could not be found in this group.');
                }
            }
        });
        return div;
    }
    unlinkRepeater(address) {
        let prompt = ui.promptMessage('Are you sure you want to stop repeating frames from this address?', () => {
            putJSONSync('/unlinkRepeater', { address: address }, (err, repeaters) => {
                if (err) ui.serviceError(err);
                else this.setRepeaterList(repeaters);
                prompt.remove();
            });

        });
    }

    unlinkRemote(shadeId, remoteAddress) {
        let prompt = ui.promptMessage('Are you sure you want to unlink this remote from the shade?', () => {
            let obj = {
                shadeId: shadeId,
                remoteAddress: remoteAddress
            };
            putJSONSync('/unlinkRemote', obj, (err, shade) => {

                console.log(shade);
                prompt.remove();
                this.setLinkedRemotesList(shade);
            });

        });
    }
    deviationChanged(el) {
        document.getElementById('spanDeviation').innerText = (el.value / 100).fmt('#,##0.00');
    }
    rxBandwidthChanged(el) {
        document.getElementById('spanRxBandwidth').innerText = (el.value / 100).fmt('#,##0.00');
    }
    frequencyChanged(el) {
        document.getElementById('spanFrequency').innerText = (el.value / 1000).fmt('#,##0.000');
    }
    txPowerChanged(el) {
        console.log(el.value);
        let lvls = [-30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12];
        document.getElementById('spanTxPower').innerText = lvls[el.value];
    }
    stepSizeChanged(el) {
        document.getElementById('spanStepSize').innerText = parseInt(el.value, 10).fmt('#,##0');
    }

    processShadeTarget(el, shadeId) {
        let positioner = document.querySelector(`.shade-positioner[data-shadeid="${shadeId}"]`);
        if (positioner) {
            positioner.querySelector(`.shade-target`).innerHTML = el.value;
            somfy.sendCommand(shadeId, el.value);
        }
    }
    processShadeTiltTarget(el, shadeId) {
        let positioner = document.querySelector(`.shade-positioner[data-shadeid="${shadeId}"]`);
        if (positioner) {
            positioner.querySelector(`.shade-tilt-target`).innerHTML = el.value;
            somfy.sendTiltCommand(shadeId, el.value);
        }
    }
    openSelectRoom() {
        this.closeShadePositioners();
        console.log('Opening rooms');
        let list = document.getElementById('divRoomSelector-list');
        list.style.display = 'block';
        document.body.addEventListener('click', () => {
            list.style.display = '';
        }, { once: true });

    }
    openSetPosition(shadeId) {
        console.log('Opening Shade Positioner');
        if (typeof shadeId === 'undefined') {
            return;
        }
        else {
            let shade = document.querySelector(`div.somfyShadeCtl[data-shadeid="${shadeId}"]`);
            if (shade) {
                let ctls = document.querySelectorAll('.shade-positioner');
                for (let i = 0; i < ctls.length; i++) {
                    console.log('Closing shade positioner');
                    ctls[i].remove();
                }
                switch (parseInt(shade.getAttribute('data-shadetype'), 10)) {
                    case 5:
                    case 9:
                    case 10:
                    case 14:
                    case 15:
                    case 16:
                        return;
                }
                let tiltType = parseInt(shade.getAttribute('data-tilt'), 10) || 0;
                let currPos = parseInt(shade.getAttribute('data-target'), 0);
                let elname = shade.querySelector(`.shadectl-name`);
                let shadeName = elname.innerHTML;
                let html = `<div class="shade-name">${shadeName}</div>`;
                let lbl = makeBool(shade.getAttribute('data-flipposition')) ? '% Open' : '% Closed';
                if (tiltType !== 3) {
                    html += `<input id="slidShadeTarget" name="shadeTarget" type="range" min="0" max="100" step="1" value="${currPos}" onchange="somfy.processShadeTarget(this, ${shadeId});" oninput="document.getElementById('spanShadeTarget').innerHTML = this.value;" />`;
                    html += `<label for="slidShadeTarget"><span>Target Position </span><span><span id="spanShadeTarget" class="shade-target">${currPos}</span><span>${lbl}</span></span></label>`;
                }
                if (tiltType > 0) {
                    let currTiltPos = parseInt(shade.getAttribute('data-tilttarget'), 10);
                    html += `<input id="slidShadeTiltTarget" name="shadeTarget" type="range" min="0" max="100" step="1" value="${currTiltPos}" onchange="somfy.processShadeTiltTarget(this, ${shadeId});" oninput="document.getElementById('spanShadeTiltTarget').innerHTML = this.value;" />`;
                    html += `<label for="slidShadeTiltTarget"><span>Target Tilt Position </span><span><span id="spanShadeTiltTarget" class="shade-tilt-target">${currTiltPos}</span><span>${lbl}</span></span></label>`;
                }
                html += `</div>`;
                let div = document.createElement('div');
                div.setAttribute('class', 'shade-positioner');
                div.setAttribute('data-shadeid', shadeId);
                div.addEventListener('onclick', (event) => { event.stopPropagation(); });
                div.innerHTML = html;
                shade.appendChild(div);
                document.body.addEventListener('click', () => {
                    let ctls = document.querySelectorAll('.shade-positioner');
                    for (let i = 0; i < ctls.length; i++) {
                        console.log('Closing shade positioner');
                        ctls[i].remove();
                    }
                }, { once: true });
            }
        }
    }
}
var somfy = new Somfy();
