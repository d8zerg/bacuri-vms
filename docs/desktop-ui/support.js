// Local renderer for the Design Component mockups in this folder.
// Supports the subset the mockups use: {{path}} holes, <sc-for>, <sc-if>,
// <helmet>, event attributes and DCLogic state. The mockups are written in English,
// buttons with a data-language attribute switch them to Russian with the texts of i18n.js.
document.write('<script src="./i18n.js"><\/script>');
(function () {
  'use strict';

  const STORAGE = 'bacuri.desktop.language';
  let language = (function () {
    const requested = new URLSearchParams(location.search).get('lang');
    if (requested === 'en' || requested === 'ru') return requested;
    try {
      const saved = localStorage.getItem(STORAGE);
      if (saved === 'en' || saved === 'ru') return saved;
    } catch (error) {
      // Storage is unavailable for local files in some browsers.
    }
    return (navigator.language || '').toLowerCase().startsWith('ru') ? 'ru' : 'en';
  })();
  const ATTRIBUTES = ['aria-label', 'title', 'placeholder', 'alt'];
  const UNITS = [
    [/(\d)\s?fps\b/g, '$1 к/с'],
    [/(\d) ms\b/g, '$1 мс'],
    [/(\d) s\b/g, '$1 с'],
    [/Mbit\/s/g, 'Мбит/с'],
    [/°\/s/g, '°/с'],
  ];

  function dictionary() {
    return window.bacuriRussian || {};
  }

  function text(value) {
    if (language !== 'ru' || typeof value !== 'string') return value;
    const russian = dictionary();
    return Object.prototype.hasOwnProperty.call(russian, value) ? russian[value] : value;
  }

  let phrases = null;
  function translate(value) {
    if (language !== 'ru' || !value || !/[A-Za-z]/.test(value)) return value;
    const russian = dictionary();
    const trimmed = value.trim();
    if (Object.prototype.hasOwnProperty.call(russian, trimmed)) return value.replace(trimmed, russian[trimmed]);
    if (!phrases) phrases = Object.keys(russian).filter((key) => key.trim().length >= 5).sort((a, b) => b.length - a.length);
    let result = value;
    for (const phrase of phrases) {
      if (result.includes(phrase)) result = result.split(phrase).join(russian[phrase]);
    }
    for (const [pattern, replacement] of UNITS) result = result.replace(pattern, replacement);
    return result;
  }

  function localize(root) {
    document.documentElement.lang = language;
    if (language === 'ru') {
      const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
      const nodes = [];
      while (walker.nextNode()) nodes.push(walker.currentNode);
      for (const node of nodes) {
        const parent = node.parentNode && node.parentNode.localName;
        if (parent === 'script' || parent === 'style') continue;
        const value = translate(node.nodeValue);
        if (value !== node.nodeValue) node.nodeValue = value;
      }
      for (const element of root.querySelectorAll('*')) {
        for (const name of ATTRIBUTES) {
          const value = element.getAttribute(name);
          if (value) element.setAttribute(name, translate(value));
        }
      }
      if (document.title) document.title = translate(document.title);
    }
    for (const button of root.querySelectorAll('[data-language]')) {
      const active = button.getAttribute('data-language') === language;
      button.setAttribute('aria-pressed', String(active));
      button.style.background = active ? '#504a37' : 'transparent';
      button.style.color = active ? '#f0ece6' : '#a6a6a6';
    }
  }

  window.bacuriLanguage = () => language;
  window.bacuriText = text;
  const localizeAll = () => localize(document.body);
  let refresh = localizeAll;
  document.addEventListener('click', (event) => {
    const button = event.target.closest && event.target.closest('[data-language]');
    if (!button) return;
    const next = button.getAttribute('data-language');
    if (next === language) return;
    try {
      localStorage.setItem(STORAGE, next);
    } catch (error) {
      // The choice is kept for this page only.
    }
    if (next === 'en' && language === 'ru' && refresh === localizeAll) {
      language = next;
      location.reload();
      return;
    }
    language = next;
    refresh();
  });

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
    if (!template || !script) {
      localize(document.body);
      return;
    }
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
      localize(root);
      if (focusId) {
        const again = document.getElementById(focusId);
        if (again) {
          again.focus();
          if (caret != null && 'setSelectionRange' in again) again.setSelectionRange(caret, caret);
        }
      }
    };
    refresh = component.__render;
    component.__render();
    if (component.componentDidMount) component.componentDidMount();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
