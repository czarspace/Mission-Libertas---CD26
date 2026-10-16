import { registerWidgetType } from './WidgetRegistry.js';

class BaseWidget {
    constructor(title, idContainerDestino) {
        this.title = title;
        this.containerDestino = document.getElementById(idContainerDestino);
        this.data;
        this.element = document.createElement('div');
        this.element.className = 'widget';
        this.element.style.left = '20px';
        this.element.style.top = '20px';

        this.header = document.createElement('div');
        this.header.className = 'widget-header';

        this.titleSpan = document.createElement('span');
        this.titleSpan.innerText = this.title;

        this.closeWidget = document.createElement('div');
        this.closeWidget.className = 'widget-close';
        this.closeWidget.innerHTML = '&times;'; 

        this.header.appendChild(this.titleSpan);
        this.header.appendChild(this.closeWidget);

        this.content = document.createElement('div');
        this.content.className = 'widget-content';

        this.resize_handle = document.createElement('div');
        this.resize_handle.className = 'widget-resize-handle';

        this.element.style.width = '300px'; 
        this.element.style.height = '200px';

        this.element.appendChild(this.header);
        this.element.appendChild(this.content);
        this.element.appendChild(this.resize_handle)

        //configuracao de eventos
        this.configEvents();
    }

    setContent(htmlString) {
        this.content.innerHTML = htmlString;
    }

    configEvents(){
        let isDragging = false;
        let isResizing = false;
        let startX, startY, initialX, initialY, initialWidth, initialHeight;
        let gridSize = 20;

        //Lógica de fechamento
        this.closeWidget.addEventListener('mousedown', (e) => {
            e.stopPropagation(); 
        });

        this.closeWidget.addEventListener('click', () => {
            this.element.remove(); 
        });

        //Lógica de resize 'snap-to-grid'
        this.header.addEventListener('mousedown',(e) => {
            isDragging = true;

            startX = e.clientX;
            startY = e.clientY;

            initialX = this.element.offsetLeft;
            initialY = this.element.offsetTop;
        })

        //Lógica de arraste 'snap-to-grid'
        this.resize_handle.addEventListener('mousedown', (e) => {
            e.stopPropagation(); 
            e.preventDefault();
            
            isResizing = true;
            startX = e.clientX;
            startY = e.clientY;
            initialWidth = this.element.offsetWidth;
            initialHeight = this.element.offsetHeight;
        });

        document.addEventListener('mousemove',(e) => {
            if (isDragging){
                let rawX = initialX + e.clientX - startX;
                let rawY = initialY + e.clientY - startY;

                let snappedX = Math.round(rawX / gridSize) * gridSize;
                let snappedY = Math.round(rawY / gridSize) * gridSize;

                const limiteLargura = this.containerDestino.clientWidth;
                const limiteAltura = this.containerDestino.clientHeight;

                const maxX = limiteLargura - this.element.offsetWidth;
                const maxY = limiteAltura - this.element.offsetHeight;

                snappedX = Math.max(0, Math.min(snappedX, maxX));
                snappedY = Math.max(0, Math.min(snappedY, maxY));

                this.element.style.left = `${snappedX}px`;
                this.element.style.top = `${snappedY}px`;
            }
            if (isResizing){
                let rawWidth = initialWidth + (e.clientX - startX);
                let rawHeight = initialHeight + (e.clientY - startY);

                let snappedWidth = Math.round(rawWidth / gridSize) * gridSize;
                let snappedHeight = Math.round(rawHeight / gridSize) * gridSize;

                const maxWidth = this.containerDestino.clientWidth - this.element.offsetLeft;
                const maxHeight = this.containerDestino.clientHeight - this.element.offsetTop;

                snappedWidth = Math.max(260, Math.min(snappedWidth, maxWidth));
                snappedHeight = Math.max(200, Math.min(snappedHeight, maxHeight));

                this.element.style.width = `${snappedWidth}px`;
                this.element.style.height = `${snappedHeight}px`;
            }
        })

        document.addEventListener('mouseup',() =>{
            isDragging = false;
            isResizing = false;
        } )


    }
    //Retorna o tipo do widget, usado para salvar a disposição dos widgets
    getKind() {
        return this.constructor.widgetType || 'widget';
    }

    //Converte para json para salvar a disposição do widget. Em novos widgest é importante colocar informações adicionais que devem ser guardadas
    serialize() {
        return {
            type: this.getKind(),
            title: this.title,
            left: this.element.style.left || '20px',
            top: this.element.style.top || '20px',
            width: this.element.offsetWidth,
            height: this.element.offsetHeight
        };
    }

    //Restaura a disposição do widget a partir de um json.
    restoreLayout(spec) {
        if (!spec) return;
        if (spec.left != null) this.element.style.left = spec.left;
        if (spec.top != null) this.element.style.top = spec.top;
        if (spec.width != null) this.element.style.width = `${spec.width}px`;
        if (spec.height != null) this.element.style.height = `${spec.height}px`;
    }

    //Limpa o widget da tela
    cleanup() {
        if (this.element.parentElement) {
            this.element.remove();
        }
    }

    render() {
        if (this.containerDestino) {
            this.containerDestino.appendChild(this.element);
            this.fitWithinContainer();
        } else {
            console.error("Contêiner de destino não encontrado");
        }
    }

    fitWithinContainer() {
        const containerWidth = this.containerDestino.clientWidth;
        const containerHeight = this.containerDestino.clientHeight;
        if (!containerWidth || !containerHeight) return;

        const minimumWidth = Math.min(260, containerWidth);
        const minimumHeight = Math.min(200, containerHeight);
        let left = Number.parseFloat(this.element.style.left) || 0;
        let top = Number.parseFloat(this.element.style.top) || 0;

        left = Math.max(0, Math.min(left, containerWidth - minimumWidth));
        top = Math.max(0, Math.min(top, containerHeight - minimumHeight));

        const width = Math.max(
            minimumWidth,
            Math.min(this.element.offsetWidth, containerWidth - left)
        );
        const height = Math.max(
            minimumHeight,
            Math.min(this.element.offsetHeight, containerHeight - top)
        );

        this.element.style.left = `${left}px`;
        this.element.style.top = `${top}px`;
        this.element.style.width = `${width}px`;
        this.element.style.height = `${height}px`;
    }
    update(new_data){
        this.data = new_data;
    }

    clearData() {
        this.data = undefined;
    }
}

registerWidgetType('widget', BaseWidget);

export default BaseWidget;
