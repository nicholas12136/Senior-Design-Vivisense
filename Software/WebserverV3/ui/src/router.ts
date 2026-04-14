import { renderHeader, initHeader } from './components/header.js';
import { renderController, initController } from './pages/controller.js';
import { renderFeedbackConfig, initFeedbackConfig } from './pages/feedbackConfig.js';
import { connectWebSocket } from './utils/websocket.js';

function getRoute(): string {
  return window.location.hash || '#controller';
}

function route(): void {
  const hash = getRoute();
  const app = document.getElementById('app');
  if (!app) return;

  let pageHTML: string;
  let pageInit: () => void;

  if (hash === '#feedback-config') {
    pageHTML = renderFeedbackConfig();
    pageInit = initFeedbackConfig;
  } else {
    pageHTML = renderController();
    pageInit = initController;
  }

  app.innerHTML = renderHeader(hash) + `<div id="page-content">${pageHTML}</div>`;
  initHeader();
  connectWebSocket(); // refresh dot status on new DOM
  pageInit();
}

export function initRouter(): void {
  window.addEventListener('hashchange', route);
  route();
}
