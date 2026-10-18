import BaseWidget from './widgets/BaseWidget.js';
import terminalWidget from './widgets/terminalWidget.js';
import chartWidget from './widgets/ChartWidget.js';
import MapWidget from './widgets/MapWidget.js';
import { createWidgetFromSpec as restoreWidgetFromSpec } from './widgets/WidgetRegistry.js';

const socket = io();
let current_data = {};
const widget_list = [];
let savedModes = [];
let currentMode = null;
let viewMode = "live";
let historicalRequestId = 0;
let historicalTimer = null;
let historicalPlaybackToken = 0;
const dataRateWindowMs = 5000;
const receivedPacketTimes = [];
let dataRateStartedAt = Date.now();

function setLinkStatus(connected) {
    const status = document.getElementById('link-status');
    if (!status) return;
    status.classList.toggle('connected', connected);
    status.innerHTML = `<i class="status-dot"></i> ${connected ? 'NOMINAL' : 'OFFLINE'}`;
}

function formatLocalTime(date) {
    return date.toLocaleTimeString('pt-BR', {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hour12: false
    });
}

function updateLocalClock() {
    const clock = document.getElementById('local-clock');
    if (clock) clock.textContent = formatLocalTime(new Date());
}

function updateDataRate() {
    const now = Date.now();
    const cutoff = now - dataRateWindowMs;
    while (receivedPacketTimes[0] < cutoff) receivedPacketTimes.shift();

    const sampledDurationMs = Math.min(
        dataRateWindowMs,
        Math.max(1000, now - dataRateStartedAt)
    );
    const packetsPerSecond = receivedPacketTimes.length / (sampledDurationMs / 1000);
    const rate = document.getElementById('data-rate');
    if (rate) rate.textContent = `${packetsPerSecond.toFixed(1)} pkt/s`;
}

socket.on('connection_status', (data) => setLinkStatus(data?.connected === true));
socket.on('connect', () => {
    dataRateStartedAt = Date.now();
    receivedPacketTimes.length = 0;
    setLinkStatus(true);
    updateDataRate();
});
socket.on('disconnect', () => {
    receivedPacketTimes.length = 0;
    setLinkStatus(false);
    updateDataRate();
});

socket.on('new_data', (data) => {
    if (!data || typeof data.values !== 'object' || Array.isArray(data.values)) return;
    receivedPacketTimes.push(Date.now());
    updateDataRate();
    const packetTime = document.getElementById('last-packet-time');
    if (packetTime) {
        const received = new Date(data.receivedTimestamp || Date.now());
        packetTime.textContent = Number.isNaN(received.getTime())
            ? '--:--:--'
            : formatLocalTime(received);
    }
    if (currentMode && currentMode.code !== data.modeCode) return;

    current_data = {
        ...data.values,
        timestamp: data.timestamp,
        collectionTimestamp: data.timestamp,
        receivedTimestamp: data.receivedTimestamp,
        modeCode: data.modeCode
    };
    
    if (viewMode === "live") {
        updateWidgets(current_data);
    }
});

socket.on('historical_data', (historicalData) => {
    if (viewMode !== 'historical') return;

    const response = typeof historicalData === 'string'
        ? JSON.parse(historicalData)
        : historicalData;

    if (response.requestId !== historicalRequestId) return;
    setHistoryLoading(false);

    if (!Array.isArray(response.samples) || response.samples.length === 0) {
        setHistoryStatus('Nenhum dado encontrado no intervalo selecionado.');
        return;
    }

    const speed = Number(document.getElementById('history-speed')?.value) || 1;
    playHistoricalSamples(response.samples, speed);
});

socket.on('historical_error', (error) => {
    if (viewMode !== 'historical') return;
    if (error?.requestId !== historicalRequestId) return;
    setHistoryLoading(false);
    setHistoryStatus(error?.message || 'Não foi possível recuperar o histórico.');
});

function updateWidgets(data_source) {
    for (let widget of widget_list) {
        widget.update(data_source);
    }
}

function sendSerialCommand(command, callback) {
    socket.timeout(5000).emit('serial_command', { command }, (error, response) => {
        callback(error ? {
            ok: false,
            message: 'Tempo limite excedido ao enviar o comando.'
        } : response || {
            ok: false,
            message: 'O backend não confirmou o envio do comando.'
        });
    });
}

function clearWidgetData() {
    for (const widget of widget_list) {
        if (typeof widget.clearData === 'function') {
            widget.clearData();
        }
    }
}

function setHistoryStatus(message) {
    const status = document.getElementById('history-status');
    if (status) status.textContent = message;
}

function setHistoryLoading(isLoading) {
    const button = document.getElementById('fetch-history-btn');
    if (!button) return;
    button.disabled = isLoading;
    button.textContent = isLoading ? 'Buscando...' : 'Reproduzir';
}

let availabilityRequestId = 0;

function toLocalDateTimeInput(date) {
    const local = new Date(date.getTime() - date.getTimezoneOffset() * 60000);
    return local.toISOString().slice(0, 16);
}

function selectAvailabilityDay(dateText) {
    const startInput = document.getElementById('hist-start');
    const endInput = document.getElementById('hist-end');
    const start = new Date(`${dateText}T00:00:00.000Z`);
    const end = new Date(start.getTime() + 24 * 60 * 60 * 1000 - 1);
    if (startInput) startInput.value = toLocalDateTimeInput(start);
    if (endInput) endInput.value = toLocalDateTimeInput(end);
    setHistoryStatus(`Intervalo de ${dateText} selecionado.`);
}

function renderTelemetryAvailability(data) {
    const track = document.getElementById('availability-track');
    const startLabel = document.getElementById('availability-start');
    const endLabel = document.getElementById('availability-end');
    if (!track || !startLabel || !endLabel) return;

    track.replaceChildren();
    const days = Array.isArray(data?.days) ? data.days : [];
    if (!data?.start || !data?.end || days.length === 0) {
        startLabel.textContent = '--';
        endLabel.textContent = '--';
        const empty = document.createElement('span');
        empty.className = 'availability-empty';
        empty.textContent = 'Nenhuma telemetria armazenada para este modo';
        track.appendChild(empty);
        return;
    }

    const formatter = new Intl.DateTimeFormat('pt-BR', {
        day: '2-digit', month: '2-digit', year: '2-digit',
        hour: '2-digit', minute: '2-digit'
    });
    startLabel.textContent = formatter.format(new Date(data.start));
    endLabel.textContent = formatter.format(new Date(data.end));

    const firstDay = Date.parse(`${days[0].date}T00:00:00Z`);
    const lastDay = Date.parse(`${days[days.length - 1].date}T00:00:00Z`);
    const dayMs = 24 * 60 * 60 * 1000;
    const totalDays = Math.max(1, Math.round((lastDay - firstDay) / dayMs) + 1);
    const maximumCount = Math.max(...days.map(day => Number(day.count) || 0), 1);

    days.forEach((day) => {
        const dayTime = Date.parse(`${day.date}T00:00:00Z`);
        const index = Math.round((dayTime - firstDay) / dayMs);
        const segment = document.createElement('button');
        segment.type = 'button';
        segment.className = 'availability-segment';
        segment.style.left = `${(index / totalDays) * 100}%`;
        segment.style.width = `max(3px, ${(1 / totalDays) * 100}%)`;
        segment.style.opacity = String(0.4 + 0.6 * ((Number(day.count) || 0) / maximumCount));
        segment.title = `${day.date}: ${day.count} amostra(s). Clique para selecionar.`;
        segment.setAttribute('aria-label', segment.title);
        segment.addEventListener('click', () => selectAvailabilityDay(day.date));
        track.appendChild(segment);
    });
}

async function loadTelemetryAvailability() {
    const requestId = ++availabilityRequestId;
    const modeCode = currentMode?.code || '';
    const query = modeCode ? `?modeCode=${encodeURIComponent(modeCode)}` : '';
    try {
        const response = await fetch(`/telemetry-availability${query}`);
        const data = await response.json();
        if (!response.ok) throw new Error(data.message || 'Falha ao consultar disponibilidade.');
        if (requestId === availabilityRequestId) renderTelemetryAvailability(data);
    } catch (error) {
        if (requestId !== availabilityRequestId) return;
        renderTelemetryAvailability(null);
        setHistoryStatus(error.message || 'Não foi possível consultar a disponibilidade.');
    }
}

function stopHistoricalPlayback(message = '') {
    historicalPlaybackToken += 1;
    if (historicalTimer !== null) {
        clearTimeout(historicalTimer);
        historicalTimer = null;
    }

    const stopButton = document.getElementById('stop-history-btn');
    if (stopButton) stopButton.style.display = 'none';
    if (message) setHistoryStatus(message);
}

function playHistoricalSamples(samples, speed) {
    stopHistoricalPlayback();
    clearWidgetData();

    const token = historicalPlaybackToken;
    const stopButton = document.getElementById('stop-history-btn');
    if (stopButton) stopButton.style.display = '';

    const playSample = (index) => {
        if (token !== historicalPlaybackToken || viewMode !== 'historical') return;

        updateWidgets(samples[index]);
        setHistoryStatus(`Reproduzindo ${index + 1} de ${samples.length} (${speed}x)`);

        if (index >= samples.length - 1) {
            historicalTimer = null;
            if (stopButton) stopButton.style.display = 'none';
            setHistoryStatus(`Reprodução concluída: ${samples.length} amostras.`);
            return;
        }

        const currentTime = Date.parse(samples[index].timestamp);
        const nextTime = Date.parse(samples[index + 1].timestamp);
        const elapsed = Number.isFinite(currentTime) && Number.isFinite(nextTime)
            ? Math.max(0, nextTime - currentTime)
            : 0;

        historicalTimer = setTimeout(() => playSample(index + 1), elapsed / speed);
    };

    playSample(0);
}

function registerWidget(widget) {
    widget_list.push(widget);

    widget.closeWidget.addEventListener('click', () => {
        const index = widget_list.indexOf(widget);
        if (index !== -1) {
            widget_list.splice(index, 1);
        }
    });

    return widget;
}

function clearWorkspace() {
    for (let widget of widget_list) {
        if (typeof widget.cleanup === 'function') {
            widget.cleanup();
        } else if (widget.element && widget.element.parentElement) {
            widget.element.remove();
        }
    }
    widget_list.length = 0;
}

function createWidgetFromSpec(spec) {
    return restoreWidgetFromSpec(spec, {
        containerId: 'workspace',
        data: current_data,
        sendCommand: sendSerialCommand
    });
}

function isValidWidgetSpec(spec) {
    if (!spec || typeof spec !== 'object') return false;

    const hasInvalidWidth = spec.width != null && Number(spec.width) <= 0;
    const hasInvalidHeight = spec.height != null && Number(spec.height) <= 0;
    return !hasInvalidWidth && !hasInvalidHeight;
}

async function fetchModesFromServer() {
    try {
        const response = await fetch('/modes');
        if (!response.ok) throw new Error('Falha ao carregar modos');
        const data = await response.json();
        savedModes = Array.isArray(data.modes) ? data.modes : [];
    } catch (error) {
        console.error(error);
        savedModes = [];
    }
}

async function saveModesToServer() {
    const controller = new AbortController();
    const timeout = window.setTimeout(() => controller.abort(), 8000);
    try {
        const response = await fetch('/modes', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Accept': 'application/json'
            },
            body: JSON.stringify({ modes: savedModes }),
            signal: controller.signal
        });
        const contentType = response.headers.get('content-type') || '';
        const data = contentType.includes('application/json')
            ? await response.json()
            : { message: `Resposta inesperada do servidor (HTTP ${response.status}).` };
        if (!response.ok) throw new Error(data.message || 'Falha ao salvar modos');
        if (Array.isArray(data.modes) && data.modes.length === savedModes.length) {
            data.modes.forEach((mode, index) => Object.assign(savedModes[index], mode));
        }
        return true;
    } catch (error) {
        console.error(error);
        const message = error.name === 'AbortError'
            ? 'O servidor demorou demais para responder.'
            : error instanceof TypeError
                ? 'Não foi possível conectar ao backend. Reinicie o servidor Flask e tente novamente.'
                : error.message;
        alert(message || 'Não foi possível salvar os modos.');
        return false;
    } finally {
        window.clearTimeout(timeout);
    }
}

function loadMode(mode) {
    clearWorkspace();
    currentMode = mode;
    window.currentMode = mode;
    if (!Array.isArray(mode.widgets)) return;

    mode.widgets.filter(isValidWidgetSpec).forEach((widgetSpec) => {
        const widget = registerWidget(createWidgetFromSpec(widgetSpec));
        widget.render();
    });

    if (viewMode === 'live') {
        updateWidgets(current_data);
    } else {
        stopHistoricalPlayback('Modo alterado. Selecione um intervalo para reproduzir.');
    }
}

function getCurrentModeSpec(name) {
    const existingMode = savedModes.find(m => m.name === name);
    
    return {
        name,
        code: existingMode?.code || '',
        dataTypes: existingMode && existingMode.dataTypes ? existingMode.dataTypes : [],
        widgets: widget_list.filter((widget) => widget.element?.isConnected).map((widget) => {
            if (typeof widget.serialize === 'function') {
                return widget.serialize();
            }
            return {
                type: 'widget',
                title: widget.title || 'Widget'
            };
        })
    };
}

function renderModesUI() {
    const modesTab = document.getElementById('modes-tab');
    const settingsModeList = document.getElementById('settingsModeList');
    
    modesTab.innerHTML = '';
    settingsModeList.innerHTML = '';

    savedModes.forEach((mode, modeIndex) => {
        if (!mode.dataTypes) mode.dataTypes = [];

        const modeButton = document.createElement('div');
        modeButton.className = 'button';
        modeButton.textContent = `${mode.code || '--'} · ${mode.name}`;
        if (currentMode === mode || (!currentMode && modeIndex === 0)) {
            modeButton.classList.add('active-mode');
        }
        
        modeButton.addEventListener('click', (event) => {
            document.querySelectorAll('#modes-tab .button').forEach(btn => btn.classList.remove('active-mode'));
            event.target.classList.add('active-mode');
            loadMode(mode);
            if (viewMode === 'historical') loadTelemetryAvailability();
        });
        modesTab.appendChild(modeButton);

        const card = document.createElement('div');
        card.className = 'mode-card';

        const cardHeader = document.createElement('div');
        cardHeader.className = 'mode-header';
        
        const nameSpan = document.createElement('span');
        nameSpan.textContent = mode.name;
        
        const deleteBtn = document.createElement('button');
        deleteBtn.className = 'delete-mode-btn';
        deleteBtn.title = 'Deletar Modo';
        deleteBtn.innerHTML = `
            <svg width="18" height="18" style="pointer-events: none; fill: none">
                <use href="#trash-svg"></use>
            </svg>
        `;
        deleteBtn.addEventListener('click', async () => {
            if (confirm(`Deletar o modo "${mode.name}"?`)) {
                savedModes = savedModes.filter(m => m.name !== mode.name);
                await saveModesToServer();
                renderModesUI();
            }
        });

        cardHeader.appendChild(nameSpan);
        cardHeader.appendChild(deleteBtn);
        card.appendChild(cardHeader);

        const codeLabel = document.createElement('label');
        codeLabel.className = 'mode-code-label';
        codeLabel.textContent = 'Código hexadecimal';
        const codeInput = document.createElement('input');
        codeInput.className = 'mode-code-input';
        codeInput.type = 'text';
        codeInput.placeholder = '0x01';
        codeInput.value = mode.code || '';
        codeInput.addEventListener('change', async () => {
            const previousCode = mode.code || '';
            mode.code = codeInput.value.trim();
            if (!await saveModesToServer()) {
                mode.code = previousCode;
                codeInput.value = previousCode;
                return;
            }
            renderModesUI();
        });
        codeLabel.appendChild(codeInput);
        card.appendChild(codeLabel);

        const dataSection = document.createElement('div');
        dataSection.className = 'data-types-section';

        const dataHeader = document.createElement('div');
        dataHeader.className = 'data-types-header';

        const addDataBtn = document.createElement('button');
        addDataBtn.className = 'add-datatype-btn';
        addDataBtn.textContent = '+';
        addDataBtn.addEventListener('click', async () => {
            const newType = await askInput('Tipo de dado:');
            if (newType) {
                mode.dataTypes.push(newType);
                await saveModesToServer();
                renderModesUI();
            }
        });
        dataHeader.appendChild(addDataBtn);
        dataSection.appendChild(dataHeader);

        const dataList = document.createElement('ul');
        dataList.className = 'data-types-list';

        let draggedItemIndex = null;

        mode.dataTypes.forEach((dataType, dtIndex) => {
            const dtItem = document.createElement('li');
            dtItem.className = 'data-type-item';
            dtItem.textContent = dataType;
            dtItem.draggable = true; 

            dtItem.addEventListener('dragstart', (e) => {
                draggedItemIndex = dtIndex;
                dtItem.classList.add('dragging');
                e.dataTransfer.effectAllowed = 'move';
            });

            dtItem.addEventListener('dragend', () => {
                dtItem.classList.remove('dragging');
                document.querySelectorAll('.data-type-item').forEach(el => el.classList.remove('drag-over'));
            });

            dtItem.addEventListener('dragover', (e) => {
                e.preventDefault(); 
                dtItem.classList.add('drag-over');
            });

            dtItem.addEventListener('dragleave', () => {
                dtItem.classList.remove('drag-over');
            });

            dtItem.addEventListener('drop', async (e) => {
                e.preventDefault();
                dtItem.classList.remove('drag-over');
                
                if (draggedItemIndex !== null && draggedItemIndex !== dtIndex) {
                    const itemToMove = mode.dataTypes.splice(draggedItemIndex, 1)[0];
                    mode.dataTypes.splice(dtIndex, 0, itemToMove);
                    
                    await saveModesToServer();
                    renderModesUI(); 
                }
            });

            const removeDtBtn = document.createElement('span');
            removeDtBtn.textContent = '×';
            removeDtBtn.style.cursor = 'pointer';
            removeDtBtn.style.color = '#ef4444';
            removeDtBtn.addEventListener('click', async () => {
                mode.dataTypes.splice(dtIndex, 1);
                await saveModesToServer();
                renderModesUI();
            });
            
            dtItem.appendChild(removeDtBtn);
            dataList.appendChild(dtItem);
        });

        dataSection.appendChild(dataList);
        card.appendChild(dataSection);
        settingsModeList.appendChild(card);
    });
} 

function askInput(title) {
    return new Promise((resolve) => {
        const modal = document.getElementById('inputModal');
        const titleEl = document.getElementById('inputModalTitle');
        const inputEl = document.getElementById('inputModalField');
        const confirmBtn = document.getElementById('inputModalConfirm');
        const cancelBtn = document.getElementById('inputModalCancel');

        titleEl.textContent = title;
        inputEl.value = '';
        modal.showModal();

        const onConfirm = () => { cleanup(); resolve(inputEl.value.trim()); };
        const onCancel = () => { cleanup(); resolve(null); };

        const cleanup = () => {
            confirmBtn.removeEventListener('click', onConfirm);
            cancelBtn.removeEventListener('click', onCancel);
            modal.close();
        };

        confirmBtn.addEventListener('click', onConfirm);
        cancelBtn.addEventListener('click', onCancel);
    });
}

document.addEventListener('DOMContentLoaded', async () => {
    updateLocalClock();
    updateDataRate();
    window.setInterval(() => {
        updateLocalClock();
        updateDataRate();
    }, 1000);
    const settingsButton = document.getElementById('settingsButton');
    const settingsMenu = document.getElementById('settingsMenu');
    const closeSettingsBtn = document.getElementById('closeSettingsBtn');
    const saveModeButton = document.getElementById('saveModeButton');
    const createModeButton = document.getElementById('createModeButton');

    if (settingsButton) {
        settingsButton.addEventListener('click', () => {
            settingsMenu.classList.add('open');
        });
    }

    closeSettingsBtn.addEventListener('click', () => {
        settingsMenu.classList.remove('open');
    });

    document.querySelectorAll('.settings-tab').forEach((tab) => {
        tab.addEventListener('click', () => {
            setActiveTab(tab.dataset.target);
        });
    });

    const createNewTestButton = document.getElementById('testButton');
    if (createNewTestButton) {
        createNewTestButton.addEventListener('click', () => {
            const widget = registerWidget(new BaseWidget('testWidget', 'workspace'));
            widget.render();
        });
    }

    const createRawDataButton = document.getElementById('rawDataButton');
    if (createRawDataButton) {
        createRawDataButton.addEventListener('click', () => {
            const widget = registerWidget(new terminalWidget(
                'Terminal',
                'workspace',
                current_data,
                sendSerialCommand
            ));
            widget.render();
        });
    }

    const createChartButton = document.getElementById('chartButton');
    if (createChartButton) {
        createChartButton.addEventListener('click', () => {
            const widget = registerWidget(new chartWidget('Gráfico', 'workspace', current_data));
            widget.render();
        });
    }

    const createMaptButton = document.getElementById('mapButton');
    if (createMaptButton) {
        createMaptButton.addEventListener('click', () => {
            const widget = registerWidget(new MapWidget('Mapa', 'workspace', current_data));
            widget.render();
        });
    }
    if (saveModeButton) {
        saveModeButton.addEventListener('click', async () => {
            if (!currentMode) {
                alert('Selecione um modo antes de atualizá-lo.');
                return;
            }

            const modeIndex = savedModes.indexOf(currentMode);
            if (modeIndex === -1) {
                alert('Não foi possível localizar o modo atual.');
                return;
            }

            const updatedMode = getCurrentModeSpec(currentMode.name);
            updatedMode.code = currentMode.code;
            updatedMode.dataTypes = [...(currentMode.dataTypes || [])];
            Object.assign(savedModes[modeIndex], structuredClone(updatedMode));
            currentMode = savedModes[modeIndex];
            window.currentMode = currentMode;

            if (await saveModesToServer()) renderModesUI();
        });
    }

    if (createModeButton) {
        createModeButton.addEventListener('click', async () => {
            const name = await askInput('Nome do novo modo:');
            if (!name) return;

            if (savedModes.some((mode) => mode.name === name)) {
                alert(`Já existe um modo chamado "${name}".`);
                return;
            }

            const code = await askInput('Código hexadecimal do modo (ex.: 0x01):');
            if (!code) return;

            const newMode = getCurrentModeSpec(name);
            newMode.code = code;
            savedModes.push(newMode);

            if (await saveModesToServer()) renderModesUI();
        });
    }

    const addWidgetBtn = document.getElementById('addWidgetBtn');
    const widgetDropdown = document.getElementById('widgetDropdown');

    if (addWidgetBtn && widgetDropdown) {
        addWidgetBtn.addEventListener('click', (e) => {
            e.stopPropagation(); 
            widgetDropdown.classList.toggle('show');
        });

        document.addEventListener('click', (e) => {
            if (!widgetDropdown.contains(e.target) && !addWidgetBtn.contains(e.target)) {
                widgetDropdown.classList.remove('show');
            }
        });

        widgetDropdown.querySelectorAll('button').forEach(btn => {
            btn.addEventListener('click', () => {
                widgetDropdown.classList.remove('show');
            });
        });
    }

const historyControls = document.getElementById('history-controls');
    const histStart = document.getElementById('hist-start');
    const histEnd = document.getElementById('hist-end');
    const fetchHistoryBtn = document.getElementById('fetch-history-btn');
    const stopHistoryBtn = document.getElementById('stop-history-btn');

    document.getElementById('btn-live').addEventListener('click', () => {
        viewMode = 'live';
        historicalRequestId += 1;
        stopHistoricalPlayback();
        setHistoryLoading(false);
        setHistoryStatus('');
        if (historyControls) historyControls.style.display = 'none'; 
        
        document.getElementById('btn-live').classList.add('active-source');
        document.getElementById('btn-history').classList.remove('active-source');
        updateWidgets(current_data);
    });

    document.getElementById('btn-history').addEventListener('click', () => {
        viewMode = 'historical';
        stopHistoricalPlayback();
        if (historyControls) historyControls.style.display = 'flex'; 
        
        document.getElementById('btn-history').classList.add('active-source');
        document.getElementById('btn-live').classList.remove('active-source');
        loadTelemetryAvailability();

        if (histEnd && !histEnd.value) {
            const now = new Date();
            now.setMinutes(now.getMinutes() - now.getTimezoneOffset()); 
            histEnd.value = now.toISOString().slice(0, 16); 
            
            const past = new Date(now.getTime() - 60 * 60 * 1000); 
            histStart.value = past.toISOString().slice(0, 16);
        }
    });

    if (fetchHistoryBtn) {
        fetchHistoryBtn.addEventListener('click', () => {
            if (!histStart.value || !histEnd.value) {
                alert('Por favor, selecione as datas de início e fim.');
                return;
            }

            const startUTC = new Date(histStart.value).toISOString();
            const endUTC = new Date(histEnd.value).toISOString();

            if (startUTC >= endUTC) {
                setHistoryStatus('A data inicial deve ser anterior à data final.');
                return;
            }

            stopHistoricalPlayback();
            clearWidgetData();
            historicalRequestId += 1;
            setHistoryLoading(true);
            setHistoryStatus('Buscando dados...');

            socket.emit('time_series', { 
                start: startUTC, 
                end: endUTC,
                modeCode: currentMode?.code || null,
                requestId: historicalRequestId
            });
        });
    }

    if (stopHistoryBtn) {
        stopHistoryBtn.addEventListener('click', () => {
            stopHistoricalPlayback('Reprodução interrompida.');
        });
    }

    document.getElementById('availability-refresh')?.addEventListener(
        'click', loadTelemetryAvailability
    );

    await fetchModesFromServer();
    renderModesUI();
    if (savedModes.length > 0) loadMode(savedModes[0]);
});
