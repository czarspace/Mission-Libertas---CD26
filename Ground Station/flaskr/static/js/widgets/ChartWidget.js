import BaseWidget from "./BaseWidget.js";
import { registerWidgetType } from './WidgetRegistry.js';

const Chart = window.Chart;

class chartWidget extends BaseWidget {
    constructor(title, idContainerDestino, data) {
        super(title, idContainerDestino);

        this.data = data;
        this.maxPoints = 50; 
        this.buffers = {};
        this.selectedKey = null;
        this.knownKeys = new Set();
        this.lineColor = '#d03379';
        this.paused = false;
        this.functions = [];

        this.content.style.flex = '1';
        this.content.style.minHeight = '0';
        this.content.style.padding = '6px';
        this.content.style.gap = '6px';

        this.buildUI();
        this.setupCanvas();

        this.closeWidget.addEventListener('click', () => {
            this.destroyChart();
        });
    }

    destroyChart() {
        if (this.resizeObserver) {
            this.resizeObserver.disconnect();
            this.resizeObserver = null;
        }
        if (this.chart) {
            this.chart.destroy();
            this.chart = null;
        }
    }

    cleanup() {
        this.destroyChart();
        super.cleanup();
    }

    serialize() {
        return {
            ...super.serialize(),
            type: this.getKind(),
            selectedKey: this.selectedKey,
            lineColor: this.lineColor,
            paused: this.paused,
            // Não exponha a referência interna: este objeto é gravado em instance/config.json.
            functions: this.functions.map((item, index) => ({
                expression: item.expression,
                color: item.color || this.getFunctionColor(index)
            }))
        };
    }

    applyState(state = {}) {
        if (state.lineColor) {
            this.lineColor = state.lineColor;
            if (this.colorInput) {
                this.colorInput.value = state.lineColor;
            }
        }
        if (typeof state.paused === 'boolean') {
            this.paused = state.paused;
            if (this.pauseBtn) {
                this.pauseBtn.innerText = this.paused ? 'Retomar' : 'Pausar';
                this.pauseBtn.style.backgroundColor = this.paused ? '#374151' : '#1f2937';
            }
        }
        if (state.selectedKey) {
            this.selectedKey = state.selectedKey;
            if (this.select) {
                this.select.value = state.selectedKey;
            }
        }
        const savedFunctions = Array.isArray(state.functions)
            ? state.functions
            : state.customFunctions; // compatibilidade com a primeira versão da funcionalidade
        if (Array.isArray(savedFunctions)) {
            this.functions = savedFunctions
                .filter(item => item && item.expression)
                .map((item, index) => ({
                    expression: item.expression,
                    color: item.color || this.getFunctionColor(index)
                }));
            this.renderFunctions();
        }
    }

    buildUI() {
        this.configBar = document.createElement('div');
        this.configBar.style.display = 'flex';
        this.configBar.style.alignItems = 'center';
        this.configBar.style.justifyContent = 'space-between';
        this.configBar.style.gap = '8px';
        this.configBar.style.flexShrink = '0';
        this.configBar.style.flexWrap = 'wrap'; 

        this.select = document.createElement('select');
        this.select.style.backgroundColor = '#1f2937';
        this.select.style.color = '#e0e6ed';
        this.select.style.border = '1px solid #374151';
        this.select.style.borderRadius = '4px';
        this.select.style.padding = '3px 6px';
        this.select.style.fontFamily = 'inherit';
        this.select.style.fontSize = '0.8rem';
        this.select.style.cursor = 'pointer';
        this.select.addEventListener('mousedown', (e) => e.stopPropagation());
        this.select.addEventListener('change', () => {
            this.selectedKey = this.select.value;
            this.draw();
        });

        const placeholder = document.createElement('option');
        placeholder.value = '';
        placeholder.innerText = 'aguardando dados...';
        this.select.appendChild(placeholder);

        this.pauseBtn = this.makeButton('Pausar');
        this.pauseBtn.addEventListener('click', () => {
            this.paused = !this.paused;
            this.pauseBtn.innerText = this.paused ? 'Retomar' : 'Pausar';
            this.pauseBtn.style.backgroundColor = this.paused ? '#374151' : '#1f2937';
        });

        this.resetBtn = this.makeButton('Reset');
        this.resetBtn.addEventListener('click', () => {
            this.buffers = {};
            this.draw();
        });

        this.colorInput = document.createElement('input');
        this.colorInput.type = 'color';
        this.colorInput.value = this.lineColor;
        this.colorInput.title = 'Cor da linha';
        this.colorInput.style.width = '34px';
        this.colorInput.style.height = '26px';
        this.colorInput.style.padding = '0';
        this.colorInput.style.border = '1px solid #374151';
        this.colorInput.style.borderRadius = '4px';
        this.colorInput.style.backgroundColor = '#1f2937';
        this.colorInput.style.cursor = 'pointer';
        this.colorInput.addEventListener('mousedown', (e) => e.stopPropagation());
        this.colorInput.addEventListener('input', () => {
            this.lineColor = this.colorInput.value;
            this.draw();
        });

        this.readout = document.createElement('span');
        this.readout.style.fontSize = '0.85rem';
        this.readout.style.color = this.lineColor;
        this.readout.style.fontWeight = 'bold';
        this.readout.style.marginLeft = 'auto'; 
        this.readout.innerText = '--';

        const controls = document.createElement('div');
        controls.style.display = 'flex';
        controls.style.alignItems = 'center';
        controls.style.gap = '6px';
        controls.style.flexWrap = 'wrap';
        controls.appendChild(this.select);
        controls.appendChild(this.pauseBtn);
        controls.appendChild(this.resetBtn);
        controls.appendChild(this.colorInput);

        this.configBar.appendChild(controls);
        this.configBar.appendChild(this.readout);

        this.settingsPanel = document.createElement('details');
        this.settingsPanel.style.flexShrink = '0';
        this.settingsPanel.style.border = '1px solid #374151';
        this.settingsPanel.style.borderRadius = '4px';
        this.settingsPanel.style.padding = '4px 6px';

        const settingsTitle = document.createElement('summary');
        settingsTitle.innerText = 'Configurações';
        settingsTitle.style.color = '#e0e6ed';
        settingsTitle.style.cursor = 'pointer';
        settingsTitle.style.fontSize = '0.75rem';
        settingsTitle.style.userSelect = 'none';

        this.functionBar = document.createElement('div');
        this.functionBar.style.display = 'flex';
        this.functionBar.style.gap = '6px';
        this.functionBar.style.alignItems = 'center';
        this.functionBar.style.flexWrap = 'wrap';
        this.functionBar.style.flexShrink = '0';
        this.functionBar.style.paddingTop = '6px';

        this.functionInput = document.createElement('input');
        this.functionInput.type = 'text';
        this.functionInput.placeholder = '';
        this.functionInput.title = '';
        this.functionInput.style.backgroundColor = '#1f2937';
        this.functionInput.style.color = '#e0e6ed';
        this.functionInput.style.border = '1px solid #374151';
        this.functionInput.style.borderRadius = '4px';
        this.functionInput.style.padding = '4px 6px';
        this.functionInput.style.fontFamily = 'monospace';
        this.functionInput.style.fontSize = '0.75rem';
        this.functionInput.style.minWidth = '200px';
        this.functionInput.style.flex = '1';
        this.functionInput.addEventListener('mousedown', (e) => e.stopPropagation());
        this.functionInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') this.addFunction();
        });

        this.addFunctionBtn = this.makeButton('+ Função');
        this.addFunctionBtn.title = 'Adicionar função ao gráfico';
        this.addFunctionBtn.addEventListener('click', () => this.addFunction());

        this.functionsList = document.createElement('div');
        this.functionsList.style.display = 'flex';
        this.functionsList.style.gap = '4px';
        this.functionsList.style.flexWrap = 'wrap';
        this.functionsList.style.width = '100%';

        this.availableDataHint = document.createElement('span');
        this.availableDataHint.style.width = '100%';
        this.availableDataHint.style.color = '#9ca3af';
        this.availableDataHint.style.fontSize = '0.7rem';
        this.availableDataHint.style.fontFamily = 'monospace';
        this.availableDataHint.innerText = '';

        this.functionBar.appendChild(this.functionInput);
        this.functionBar.appendChild(this.addFunctionBtn);
        this.functionBar.appendChild(this.availableDataHint);
        this.functionBar.appendChild(this.functionsList);
        this.settingsPanel.appendChild(settingsTitle);
        this.settingsPanel.appendChild(this.functionBar);

        this.canvasWrapper = document.createElement('div');
        this.canvasWrapper.style.position = 'relative';
        this.canvasWrapper.style.flex = '1';
        this.canvasWrapper.style.minHeight = '0';

        this.canvas = document.createElement('canvas');
        this.canvas.style.position = 'absolute';
        this.canvas.style.inset = '0';
        this.canvas.style.width = '100%';
        this.canvas.style.height = '100%';
        this.canvasWrapper.appendChild(this.canvas);

        this.content.appendChild(this.configBar);
        this.content.appendChild(this.settingsPanel);
        this.content.appendChild(this.canvasWrapper);
    }

    addFunction() {
        const expression = this.functionInput.value.trim();
        if (!expression) return;

        try {
            this.compileFunction(expression, this.getLatestValues());
        } catch (error) {
            this.functionInput.title = `Função inválida: ${error.message}`;
            this.functionInput.style.borderColor = '#ef4444';
            return;
        }

        this.functions.push({
            expression,
            color: this.getFunctionColor(this.functions.length)
        });
        this.functionInput.value = '';
        this.functionInput.title = '';
        this.functionInput.style.borderColor = '#374151';
        this.renderFunctions();
        this.draw();
    }

    removeFunction(index) {
        this.functions.splice(index, 1);
        this.renderFunctions();
        this.draw();
    }

    renderFunctions() {
        if (!this.functionsList) return;
        this.functionsList.innerHTML = '';

        this.functions.forEach((item, index) => {
            const tag = document.createElement('span');
            tag.style.display = 'inline-flex';
            tag.style.alignItems = 'center';
            tag.style.gap = '5px';
            tag.style.padding = '2px 5px';
            tag.style.backgroundColor = '#1f2937';
            tag.style.border = `1px solid ${item.color}`;
            tag.style.borderRadius = '4px';
            tag.style.color = '#e0e6ed';
            tag.style.fontFamily = 'monospace';
            tag.style.fontSize = '0.7rem';

            const text = document.createElement('span');
            text.innerText = `f = ${item.expression}`;

            const color = document.createElement('input');
            color.type = 'color';
            color.value = item.color || this.getFunctionColor(index);
            color.title = 'Cor da função';
            color.style.width = '22px';
            color.style.height = '18px';
            color.style.padding = '0';
            color.style.border = '0';
            color.style.background = 'transparent';
            color.style.cursor = 'pointer';
            color.addEventListener('mousedown', (event) => event.stopPropagation());
            color.addEventListener('input', () => {
                item.color = color.value;
                tag.style.borderColor = item.color;
                this.draw();
            });
            const remove = document.createElement('button');
            remove.innerText = '×';
            remove.title = 'Remover função';
            remove.style.border = '0';
            remove.style.padding = '0 2px';
            remove.style.background = 'transparent';
            remove.style.color = '#e0e6ed';
            remove.style.cursor = 'pointer';
            remove.addEventListener('click', () => this.removeFunction(index));

            tag.appendChild(text);
            tag.appendChild(color);
            tag.appendChild(remove);
            this.functionsList.appendChild(tag);
        });
    }

    getFunctionColor(index) {
        const colors = ['#60a5fa', '#34d399', '#fbbf24', '#a78bfa', '#fb7185', '#22d3ee'];
        return colors[index % colors.length];
    }

    getFormulaKeys(values = {}) {
        const configuredKeys = typeof window !== 'undefined' && window.currentMode
            ? (window.currentMode.dataTypes || [])
            : [];
        return [...new Set([...configuredKeys, ...this.knownKeys, ...Object.keys(values)])]
            .filter(key => /^[A-Za-z_$][A-Za-z0-9_$]*$/.test(key))
            .filter(key => key !== 'x' && key !== 'dados');
    }

    getLatestValues() {
        const values = {};
        Object.entries(this.buffers).forEach(([key, buffer]) => {
            const lastPoint = buffer[buffer.length - 1];
            if (lastPoint) values[key] = lastPoint.value;
        });
        return values;
    }

    compileFunction(expression, values = {}) {
        const keys = this.getFormulaKeys(values);
        return {
            keys,
            fn: new Function('x', 'dados', ...keys, `return (${expression});`)
        };
    }

    evaluateFunction(expression, values) {
        try {
            const safeValues = values || {};
            const { keys, fn } = this.compileFunction(expression, safeValues);
            const x = safeValues[this.selectedKey];
            const result = fn(x, safeValues, ...keys.map(key => safeValues[key]));
            return Number.isFinite(result) ? result : null;
        } catch (_) {
            return null;
        }
    }

    makeButton(label) {
        const btn = document.createElement('button');
        btn.innerText = label;
        btn.style.backgroundColor = '#1f2937';
        btn.style.color = '#e0e6ed';
        btn.style.border = '1px solid #374151';
        btn.style.borderRadius = '4px';
        btn.style.padding = '4px 8px';
        btn.style.fontFamily = 'inherit';
        btn.style.fontSize = '0.75rem';
        btn.style.cursor = 'pointer';
        btn.addEventListener('mousedown', (e) => e.stopPropagation());
        return btn;
    }

    setupCanvas() {
        this.ctx = this.canvas.getContext('2d');

        const noDataPlugin = {
            id: 'noDataText',
            afterDraw: (chart) => {
                if (!chart.data.datasets.length || chart.data.datasets[0].data.length === 0) {
                    const ctx = chart.ctx;
                    const width = chart.width;
                    const height = chart.height;
                    
                    chart.clear();
                    ctx.save();
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillStyle = '#9ca3af';
                    ctx.font = '12px monospace';
                    ctx.fillText('sem dados para "' + (this.selectedKey || '') + '"', width / 2, height / 2);
                    ctx.restore();
                }
            }
        };

        this.chart = new Chart(this.ctx, {
            type: 'line',
            data: {
                labels: [], 
                datasets: [{
                    label: 'Dado',
                    data: [],
                    borderColor: this.lineColor,
                    backgroundColor: this.lineColor,
                    borderWidth: 2,
                    pointBackgroundColor: this.lineColor,
                    tension: 0 
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: false, 
                layout: {
                    padding: { top: 10, bottom: 10, left: 10, right: 10 }
                },
                scales: {
                    x: {
                        display: true, 
                        grid: {
                            color: '#374151',
                            drawBorder: false
                        },
                        ticks: {
                            display: true,       
                            color: '#9ca3af',
                            font: { family: 'monospace', size: 12 },
                            maxTicksLimit: 10    
                        }
                    },
                    y: {
                        grid: {
                            color: '#374151',
                            drawBorder: false
                        },
                        ticks: {
                            color: '#9ca3af',
                            font: { family: 'monospace', size: 10 },
                            maxTicksLimit: 5
                        },
                        border: { display: false }
                    }
                },
                plugins: {
                    legend: {
                        display: true,
                        labels: {
                            color: '#9ca3af',
                            font: { family: 'monospace', size: 10 },
                            boxWidth: 12
                        }
                    },
                    tooltip: {
                        enabled: true,             
                        intersect: false,          
                        mode: 'index',             
                        backgroundColor: '#1f2937', 
                        titleColor: '#9ca3af',      
                        bodyColor: '#e0e6ed',       
                        borderColor: '#374151',     
                        borderWidth: 1,
                        font: {
                            family: 'monospace'    
                        },
                        callbacks: {
                            title: function(context) {
                                return 'Tempo: ' + context[0].label;
                            },
                            label: function(context) {
                                const value = context.parsed.y;
                                return context.dataset.label + ': ' + (Number.isFinite(value) ? value.toFixed(2) : '--');
                            }
                        }
                    }
                }
            },
            plugins: [noDataPlugin]
        });
  
        this.resizeObserver = new ResizeObserver(() => {
            if(this.chart) this.chart.resize();
        });
        this.resizeObserver.observe(this.canvasWrapper);
    }

refreshKeys(payload) {
        if (!payload) return;

        let validKeys = [];
        let dataSource;

        if (typeof window !== 'undefined' && window.currentMode && window.currentMode.dataTypes && window.currentMode.dataTypes.length > 0) {
            validKeys = window.currentMode.dataTypes;
            dataSource = payload;
        } else {
            dataSource = payload.values !== undefined ? payload.values : payload; 
            validKeys = Object.keys(dataSource);
        }

        validKeys = validKeys.filter(key => 
            key !== 'timestamp' && 
            key !== 'values' &&
            dataSource[key] !== undefined && 
            Number.isFinite(parseFloat(dataSource[key]))
        );

        for (const knownKey of Array.from(this.knownKeys)) {
            if (!validKeys.includes(knownKey)) {
                this.knownKeys.delete(knownKey);
                
                Array.from(this.select.options).forEach((opt, index) => {
                    if (opt.value === knownKey) this.select.remove(index);
                });
                
                delete this.buffers[knownKey];
            }
        }

        validKeys.forEach(key => {
            if (!this.knownKeys.has(key)) {
                this.knownKeys.add(key);
                
                const opt = document.createElement('option');
                opt.value = key;
                opt.innerText = key;

                if (this.select.options.length > 0 && this.select.options[0].value === '') {
                    this.select.remove(0);
                }
                
                this.select.appendChild(opt);
            }
        });

        if (this.availableDataHint) {
            this.availableDataHint.innerText = validKeys.length
                ? `Dados disponíveis: ${validKeys.join(', ')}. `
                : 'Dados disponíveis: aguardando dados...';
        }
      
        if (this.selectedKey && !validKeys.includes(this.selectedKey)) {
            this.selectedKey = validKeys.length > 0 ? validKeys[0] : null;
            this.select.value = this.selectedKey || '';
            this.draw(); 
        } 
        else if (this.selectedKey === null && validKeys.length > 0) {
            this.selectedKey = validKeys[0];
            this.select.value = validKeys[0];
        }
    }

    update(new_data) {
        const payload = new_data || this.data;
        if (payload == undefined) return;
        if (this.paused) return; 

        this.refreshKeys(payload);

        const exactTimestamp = payload.timestamp || new Date().toLocaleTimeString(); 
        
        let dataSource;
        if (typeof window !== 'undefined' && window.currentMode && window.currentMode.dataTypes && window.currentMode.dataTypes.length > 0) {
            dataSource = payload;
        } else {
            dataSource = payload.values !== undefined ? payload.values : payload;
        }

        const values = Object.fromEntries(
            Object.entries(dataSource)
                .filter(([key, value]) => key !== 'timestamp' && key !== 'values' && Number.isFinite(parseFloat(value)))
                .map(([key, value]) => [key, parseFloat(value)])
        );

        for (const [key, num] of Object.entries(values)) {
            if (!this.buffers[key]) this.buffers[key] = [];
            const buf = this.buffers[key];
            
            // Cada ponto guarda o pacote inteiro: uma fórmula pode usar qualquer dado do modo.
            buf.push({ value: num, time: exactTimestamp, values });

            if (buf.length > this.maxPoints) buf.shift();
        }

        this.draw();
    }

    clearData() {
        this.buffers = {};
        this.draw();
    }

    draw() {
        if (!this.chart) return;

        const buf = this.selectedKey ? this.buffers[this.selectedKey] : null;

        if (!buf || buf.length === 0) {
            this.readout.innerText = '--';
            this.chart.data.labels = [];
            this.chart.data.datasets = [{
                label: this.selectedKey || 'Dado',
                data: [],
                borderColor: this.lineColor,
                backgroundColor: this.lineColor,
                pointBackgroundColor: this.lineColor
            }];
            this.chart.update();
            return;
        }

        this.chart.data.labels = buf.map(item => this.formatChartTime(item.time));
        this.chart.data.datasets = [
            {
                label: this.selectedKey || 'Dado',
                data: buf.map(item => item.value),
                borderColor: this.lineColor,
                backgroundColor: this.lineColor,
                borderWidth: 2,
                pointBackgroundColor: this.lineColor,
                tension: 0
            },
            ...this.functions.map(item => ({
                label: `f = ${item.expression}`,
                data: buf.map(point => this.evaluateFunction(item.expression, point.values || {
                    [this.selectedKey]: point.value
                })),
                borderColor: item.color,
                backgroundColor: item.color,
                borderWidth: 2,
                pointRadius: 0,
                tension: 0
            }))
        ];

        this.chart.update();

        this.readout.style.color = this.lineColor;
        this.readout.innerText = buf[buf.length - 1]['value'].toFixed(2);
    }

    formatChartTime(timestamp) {
        const parsed = new Date(timestamp);
        if (!Number.isNaN(parsed.getTime())) {
            return parsed.toLocaleTimeString('pt-BR', {
                hour: '2-digit',
                minute: '2-digit',
                second: '2-digit',
                hour12: false
            });
        }

        const timeMatch = String(timestamp).match(/\b\d{2}:\d{2}:\d{2}\b/);
        return timeMatch ? timeMatch[0] : String(timestamp);
    }
}

registerWidgetType('chart', chartWidget, 'Gráfico');

export default chartWidget;
