// Lembre-se de importar a classe pai (ajuste o caminho se necessário)
import BaseWidget from './BaseWidget.js';
import { registerWidgetType } from './WidgetRegistry.js';

class MapWidget extends BaseWidget {
    constructor(title, idContainerDestino) {
        super(title, idContainerDestino);

        this.aviaoFocado = null;
        this.marcadoresAtivos = {};
        this.ultimasCoordenadas = {};
        this.ultimosAngulos = {};

        this.mapContainer = document.createElement('div');
        this.mapId = 'mapa-' + Math.random().toString(36).substring(2, 9);
        this.mapContainer.id = this.mapId;
        this.mapContainer.className = 'map-container';

        this.content.appendChild(this.mapContainer);

        this.resizeObserver = new ResizeObserver(() => {
            if (this.map) {
                this.map.invalidateSize();
            }
        });
        this.resizeObserver.observe(this.content);

        this.closeWidget.addEventListener('click', () => this.destroyMap());
    }

    render() {
        super.render();
        
        this.renderTimer = setTimeout(() => {
            this.renderTimer = null;
            if (!this.mapContainer.isConnected) return;

            this.map = L.map(this.mapId).setView([-19.9167, -43.9345], 13);

            L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
                maxZoom: 19,
                attribution: '&copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>'
            }).addTo(this.map);
            
            const soltarCamera = () => {
                if (this.aviaoFocado !== null) {
                    let idDoAviaoSeguido = this.aviaoFocado;
                    this.aviaoFocado = null; 
                    
                    let aviao = this.marcadoresAtivos[idDoAviaoSeguido];
                    if (aviao) {
                        aviao.slideTo(aviao.getLatLng(), {
                            duration: 0,
                            keepAtCenter: false
                        });
                    }
                }
            };
            
            this.map.on('dragstart', soltarCamera);
            this.map.on('click', soltarCamera);
        }, 0);
    }

    destroyMap() {
        if (this.renderTimer != null) {
            clearTimeout(this.renderTimer);
            this.renderTimer = null;
        }
        if (this.map) {
            this.map.remove();
            this.map = null;
        }
    }

    obterCorPorAltitude(altitude) {
        if (altitude < 5000)  return '#d03379'; // Rosa (Muito Baixo)
        if (altitude < 10000) return '#d08a33'; // Laranja (Baixo)
        if (altitude < 15000) return '#d0d033'; // Amarelo
        if (altitude < 20000) return '#33d08a'; // Verde
        if (altitude < 30000) return '#33d0d0'; // Ciano
        if (altitude < 40000) return '#3379d0'; // Azul (Cruzeiro)
        return '#a333d0';                       // Roxo (Muito Alto)
    }

    // Retorna agora um objeto para sabermos se devemos avançar a âncora de coordenadas
    calcularAnguloDirecao(idVoo, latAnterior, lngAnterior, latAtual, lngAtual) {
        let dx = lngAtual - lngAnterior;
        let dy = latAtual - latAnterior;

        let distancia = Math.sqrt(dx * dx + dy * dy);
        
        // Se a distância for muito curta, devolvemos o ângulo antigo e dizemos para NÃO atualizar a âncora
        if (distancia < 0.00005) { 
            return { angulo: this.ultimosAngulos[idVoo] || 0, atualizaFiltro: false };
        }

        let anguloGraus = Math.atan2(dy, dx) * (180 / Math.PI);
        let anguloAjustado = (90 - anguloGraus + 360) % 360;

        let anguloAnterior = this.ultimosAngulos[idVoo] || 0;
        let anguloAnteriorBase = (anguloAnterior % 360 + 360) % 360; 
        
        let diferenca = anguloAjustado - anguloAnteriorBase;

        if (diferenca > 180) diferenca -= 360;
        if (diferenca <= -180) diferenca += 360; // Alterado para <= para precaver inversões exatas

        let anguloFinal = anguloAnterior + diferenca;

        this.ultimosAngulos[idVoo] = anguloFinal;
        return { angulo: anguloFinal, atualizaFiltro: true };
    }

    atualizarAviao(dadosDoVoo) {
        if (!this.map) return; 

        let idVoo = dadosDoVoo.id;
        let lat = dadosDoVoo.lat;
        let lng = dadosDoVoo.lng;

        // Recupera a âncora ou cria uma nova se for o primeiro registo
        let coordAnterior = this.ultimasCoordenadas[idVoo];
        if (!coordAnterior) {
            coordAnterior = { lat: lat, lng: lng };
            this.ultimasCoordenadas[idVoo] = coordAnterior;
        }

        let resultado = this.calcularAnguloDirecao(idVoo, coordAnterior.lat, coordAnterior.lng, lat, lng);
        let angulo = resultado.angulo;
        
        // A magia da acumulação: SÓ guardamos a posição nova se ele se moveu significativamente!
        if (resultado.atualizaFiltro) {
            this.ultimasCoordenadas[idVoo] = { lat: lat, lng: lng };
        }

        let corDin = this.obterCorPorAltitude(dadosDoVoo.altitude);

        // Adicionada uma transição de 'fill' para a cor desvanecer suavemente
        const gerarSVG = (cor, rotacao) => `
            <svg style="transform: rotate(${rotacao}deg); transform-origin: center; transition: transform 1.5s ease-out;" 
                 viewBox="0 0 24 24" width="38" height="38" xmlns="http://www.w3.org/2000/svg">
                <path d="M21 16v-2l-8-5V3.5c0-.83-.67-1.5-1.5-1.5S10 2.67 10 3.5V9l-8 5v2l8-2.5V19l-2 1.5V22l3.5-1 3.5 1v-1.5L13 19v-5.5l8 2.5z" 
                      fill="${cor}" style="transition: fill 1.5s ease-out;" />
            </svg>
        `;

        if (this.marcadoresAtivos[idVoo]) {
            let marker = this.marcadoresAtivos[idVoo];
            let deveCentralizar = (this.aviaoFocado === idVoo);

            marker.slideTo([lat, lng], {
                duration: 2000,       
                keepAtCenter: deveCentralizar,   
            });

            let textoAtualizado = `<b>${idVoo}</b><br>${dadosDoVoo.lat}<br>${dadosDoVoo.lng}<br>${dadosDoVoo.altitude} ft`;
            marker.setTooltipContent(textoAtualizado);
            
            // Em vez de recriar todo o ícone (o que quebrava a animação CSS), editamos o SVG existente diretamente no DOM!
            let iconElement = marker.getElement();
            if (iconElement) {
                let svg = iconElement.querySelector('svg');
                if (svg) {
                    svg.style.transform = `rotate(${angulo}deg)`;
                    
                    let path = svg.querySelector('path');
                    if (path) path.setAttribute('fill', corDin);
                }
            }
            
        } else {
            let icone = L.divIcon({
                html: gerarSVG(corDin, angulo),
                className: '',
                iconSize: [38, 38],
                iconAnchor: [19, 19]
            });

            let textoLateral = `<b>${idVoo}</b><br>${dadosDoVoo.lat}<br>${dadosDoVoo.lng}<br>${dadosDoVoo.altitude} ft`;
            
            let novoMarcador = L.marker([lat, lng], {icon: icone})
                .bindTooltip(textoLateral, { 
                    permanent: false,      
                    direction: 'right',    
                    offset: [15, 0],       
                    className: 'meu-tooltip' 
                })
                .addTo(this.map);
            
            novoMarcador.on('click', () => {
                if (this.aviaoFocado === idVoo) {
                    this.aviaoFocado = null;
                } else {
                    this.aviaoFocado = idVoo; 
                }
            });
            
            this.marcadoresAtivos[idVoo] = novoMarcador;
        }
    }

    update(dados) {
        if (dados && dados.avioes) {
            for (let aviaoAtual of dados.avioes) {
                this.atualizarAviao(aviaoAtual);
            }
        }
    }

    cleanup() {
        this.resizeObserver?.disconnect();
        this.resizeObserver = null;
        this.destroyMap();
        super.cleanup();
    }
}

registerWidgetType('map', MapWidget, 'Mapa');

export default MapWidget;
