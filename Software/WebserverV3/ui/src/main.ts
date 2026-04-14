import './style.css';
import { initRouter } from './router.js';
import { connectWebSocket } from './utils/websocket.js';

connectWebSocket();
initRouter();
