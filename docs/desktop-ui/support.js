// Local renderer for the Design Component mockups in this folder.
// Supports the subset the mockups use: {{path}} holes, <sc-for>, <sc-if>,
// <helmet>, event attributes and DCLogic state.
(function () {
  'use strict';

  class DCLogic {
    constructor(props) {
      this.props = props || {};
      this.state = {};
    }
    setState(patch) {
      Object.assign(this.state, typeof patch === 'function' ? patch(this.state) : patch);
      if (this.__render) this.__render();
    }
    forceUpdate() {
      if (this.__render) this.__render();
    }
  }

  const HOLE = /\{\{\s*([^}]+?)\s*\}\}/g;
  const WHOLE = /^\s*\{\{\s*([^}]+?)\s*\}\}\s*$/;
  const EVENTS = { onclick: 'click', onchange: 'input', onkeydown: 'keydown', oninput: 'input' };

  function lookup(path, scope) {
    if (path === 'true') return true;
    if (path === 'false') return false;
    if (path === 'null') return null;
    if (/^-?\d+(\.\d+)?$/.test(path)) return Number(path);
    let value = scope;
    for (const part of path.split('.')) {
      if (value == null) return undefined;
      value = value[part];
    }
    return value;
  }

  function interpolate(text, scope) {
    return text.replace(HOLE, (_, path) => {
      const value = lookup(path, scope);
      return value == null ? '' : String(value);
    });
  }

  function renderNodes(nodes, scope, parent) {
    for (const node of nodes) renderNode(node, scope, parent);
  }

  function renderNode(node, scope, parent) {
    if (node.nodeType === Node.TEXT_NODE) {
      parent.appendChild(document.createTextNode(interpolate(node.nodeValue, scope)));
      return;
    }
    if (node.nodeType !== Node.ELEMENT_NODE) return;
    const tag = node.localName;
    if (tag === 'sc-for') {
      const match = WHOLE.exec(node.getAttribute('list') || '');
      const list = match ? lookup(match[1], scope) : [];
      const name = node.getAttribute('as') || 'item';
      (list || []).forEach((item, index) => {
        renderNodes(node.childNodes, Object.assign(Object.create(scope), { [name]: item, $index: index }), parent);
      });
      return;
    }
    if (tag === 'sc-if') {
      const match = WHOLE.exec(node.getAttribute('value') || '');
      if (match && lookup(match[1], scope)) renderNodes(node.childNodes, scope, parent);
      return;
    }
    if (tag === 'helmet' || tag === 'dc-import') return;

    const element = document.createElementNS(node.namespaceURI, node.tagName.includes(':') ? node.tagName : node.localName);
    for (const attribute of Array.from(node.attributes)) {
      const name = attribute.name;
      if (name.startsWith('hint-')) continue;
      const whole = WHOLE.exec(attribute.value);
      const event = EVENTS[name.toLowerCase()];
      if (event) {
        const handler = whole ? lookup(whole[1], scope) : null;
        if (typeof handler === 'function') element.addEventListener(event, handler);
        continue;
      }
      const value = whole ? lookup(whole[1], scope) : interpolate(attribute.value, scope);
      if (name === 'value' && 'value' in element) {
        element.value = value == null ? '' : String(value);
        continue;
      }
      if (value === false || value == null) continue;
      element.setAttribute(name, value === true ? '' : String(value));
    }
    const children = node.localName === 'template' ? node.content.childNodes : node.childNodes;
    renderNodes(children, scope, element);
    parent.appendChild(element);
  }

  function start() {
    const template = document.querySelector('x-dc');
    const script = document.querySelector('script[data-dc-script]');
    if (!template || !script) return;
    template.remove();

    const helmet = template.querySelector('helmet');
    if (helmet) {
      for (const child of Array.from(helmet.children)) document.head.appendChild(child.cloneNode(true));
    }

    const fonts = document.createElement('style');
    fonts.textContent =
      "@font-face{font-family:Inter;src:url(../../design/bacuri/assets/inter-latin.woff2) format('woff2');font-weight:100 900}" +
      "@font-face{font-family:Inter;src:url(../../design/bacuri/assets/inter-cyrillic.woff2) format('woff2');font-weight:100 900;unicode-range:U+0400-045F,U+0490-0491,U+04B0-04B1,U+2116}";
    document.head.appendChild(fonts);

    let props = {};
    try {
      const declared = JSON.parse(script.getAttribute('data-props') || '{}');
      for (const key of Object.keys(declared)) {
        if (!key.startsWith('$') && declared[key] && 'default' in declared[key]) props[key] = declared[key].default;
      }
    } catch (error) {
      props = {};
    }

    const Component = new Function('DCLogic', script.textContent + '\nreturn Component;')(DCLogic);
    const component = new Component(props);
    const root = document.createElement('div');
    document.body.insertBefore(root, document.body.firstChild);

    component.__render = function () {
      const active = document.activeElement;
      const focusId = active && active.id;
      const caret = active && 'selectionStart' in active ? active.selectionStart : null;
      const fragment = document.createDocumentFragment();
      renderNodes(template.childNodes, component.renderVals(), fragment);
      root.replaceChildren(fragment);
      if (focusId) {
        const again = document.getElementById(focusId);
        if (again) {
          again.focus();
          if (caret != null && 'setSelectionRange' in again) again.setSelectionRange(caret, caret);
        }
      }
    };
    component.__render();
    if (component.componentDidMount) component.componentDidMount();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
