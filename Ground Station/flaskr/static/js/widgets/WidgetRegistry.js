const widgetTypes = new Map();

export function registerWidgetType(type, WidgetClass, defaultTitle = 'Widget') {
    if (!type || typeof WidgetClass !== 'function') {
        throw new TypeError('Tipo e classe do widget são obrigatórios.');
    }

    WidgetClass.widgetType = type;
    widgetTypes.set(type, { WidgetClass, defaultTitle });
}

export function createWidgetFromSpec(spec, context = {}) {
    const registration = widgetTypes.get(spec?.type) || widgetTypes.get('widget');
    if (!registration) {
        throw new Error('Nenhum widget base foi registrado.');
    }

    const { WidgetClass, defaultTitle } = registration;
    const widget = new WidgetClass(
        spec?.title || defaultTitle,
        context.containerId,
        context.data,
        context.sendCommand
    );

    if (typeof widget.applyState === 'function') {
        widget.applyState(spec);
    }
    if (typeof widget.restoreLayout === 'function') {
        widget.restoreLayout(spec);
    }

    return widget;
}
