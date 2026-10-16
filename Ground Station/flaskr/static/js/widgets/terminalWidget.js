import BaseWidget from "./BaseWidget.js"
import { registerWidgetType } from './WidgetRegistry.js';

class rawDataWidget extends BaseWidget{
    constructor(title, idContainerDestino, data, sendCommand){
        super(title,idContainerDestino)
        this.data = data;
        this.sendCommand = sendCommand;
        this.lineCounter = 0;

        this.content.style.display = 'flex';
        this.content.style.flexDirection = 'column';
        this.content.style.overflow = 'hidden';
        this.content.style.height = '100%';

        this.output = document.createElement('div');
        this.output.className = 'terminal-output';
        this.output.style.display = 'flex';
        this.output.style.flexDirection = 'column';
        this.output.style.flex = '1';
        this.output.style.minHeight = '0';
        this.output.style.overflow = 'auto';

        this.commandForm = document.createElement('form');
        this.commandForm.className = 'terminal-command-form';
        this.commandForm.style.display = 'flex';
        this.commandForm.style.gap = '6px';
        this.commandForm.style.padding = '6px';
        this.commandForm.style.borderTop = '1px solid #374151';
        this.commandForm.style.backgroundColor = '#111827';

        this.commandInput = document.createElement('input');
        this.commandInput.type = 'text';
        this.commandInput.placeholder = 'Digite um comando...';
        this.commandInput.autocomplete = 'off';
        this.commandInput.style.flex = '1';
        this.commandInput.style.minWidth = '0';
        this.commandInput.style.padding = '5px 7px';
        this.commandInput.style.color = '#e0e6ed';
        this.commandInput.style.backgroundColor = '#0d121e';
        this.commandInput.style.border = '1px solid #374151';
        this.commandInput.style.borderRadius = '4px';
        this.commandInput.style.fontFamily = 'monospace';

        this.commandButton = document.createElement('button');
        this.commandButton.type = 'submit';
        this.commandButton.innerText = 'Enviar';
        this.commandButton.style.padding = '5px 10px';
        this.commandButton.style.color = '#ffffff';
        this.commandButton.style.backgroundColor = '#d03379';
        this.commandButton.style.border = 'none';
        this.commandButton.style.borderRadius = '4px';
        this.commandButton.style.cursor = 'pointer';

        this.commandForm.appendChild(this.commandInput);
        this.commandForm.appendChild(this.commandButton);
        this.content.appendChild(this.output);
        this.content.appendChild(this.commandForm);

        this.commandForm.addEventListener('submit', (event) => {
            event.preventDefault();
            this.submitCommand();
        });

        this.element.style.width = '300px'; 
        this.element.style.height = '200px';
    }

    serialize() {
        return {
            ...super.serialize(),
            type: this.getKind()
        };
    }

    submitCommand() {
        const command = this.commandInput.value.trim();
        if (!command) return;

        this.appendLine(`> ${command}`, 'command');
        this.commandInput.value = '';

        if (typeof this.sendCommand !== 'function') {
            this.appendLine('Erro: envio serial indisponível.', 'error');
            return;
        }

        this.commandInput.disabled = true;
        this.commandButton.disabled = true;
        this.sendCommand(command, (response) => {
            const succeeded = response?.ok === true;
            this.appendLine(
                `${succeeded ? 'OK' : 'Erro'}: ${response?.message || 'Sem resposta do backend.'}`,
                succeeded ? 'success' : 'error'
            );
            this.commandInput.disabled = false;
            this.commandButton.disabled = false;
            this.commandInput.focus();
        });
    }

    appendLine(text, kind = 'telemetry') {
        const newLine = document.createElement('span');
        newLine.className = `widgetLine terminal-line-${kind}`;
        newLine.style.whiteSpace = 'nowrap';
        newLine.style.fontFamily = 'monospace';
        newLine.style.padding = '2px 4px';

        if (kind === 'error') newLine.style.color = '#ef4444';
        if (kind === 'success') newLine.style.color = '#22c55e';
        if (kind === 'command') newLine.style.color = '#d03379';
        if (kind === 'telemetry' && this.lineCounter % 2 === 1) {
            newLine.style.backgroundColor = '#1f2937';
        }

        const wasAtBottom = this.output.scrollHeight - this.output.scrollTop - this.output.clientHeight <= 15;
        newLine.innerText = text;
        this.output.appendChild(newLine);

        if (wasAtBottom) {
            requestAnimationFrame(() => {
                this.output.scrollTop = this.output.scrollHeight;
            });
        }
    }

    update(new_data){
        const payload = new_data || this.data;
        if(payload != undefined){
            this.data = payload;
            const timestamp = payload.timestamp ? new Date(payload.timestamp) : new Date();
            const timeString = Number.isNaN(timestamp.getTime())
                ? String(payload.timestamp)
                : timestamp.toLocaleTimeString();
            let line = "[" + timeString + "]"
            const dataSource = payload.values !== undefined ? payload.values : payload;
            for (const [key, value] of Object.entries(dataSource)){
                if (key === 'timestamp' || key === 'values') continue;
                let formatted = typeof value === 'number' ? value.toFixed(2) : value;
                line += ` ${key}: ${formatted} |`;
            }
            this.appendLine(line);
            this.lineCounter++
        }
    }

    clearData() {
        this.data = undefined;
        this.lineCounter = 0;
        this.output.replaceChildren();
    }
}

registerWidgetType('terminal', rawDataWidget, 'Terminal');

export default rawDataWidget;
