/**
 * 智能CSI闹铃 Web控制页面脚本
 */

// API基础URL
const API_BASE = '/api';

// 全局状态
let systemStatus = {
    isPersonPresent: false,
    confidence: 0,
    nextAlarm: null,
    wifiSignal: 0,
    isConnected: false
};

let alarms = [];
let logEntries = [];
let updateInterval = null;

// DOM元素
const elements = {
    detectionStatus: document.getElementById('detection-status'),
    confidence: document.getElementById('confidence'),
    nextAlarm: document.getElementById('next-alarm'),
    wifiSignal: document.getElementById('wifi-signal'),
    alarmHour: document.getElementById('alarm-hour'),
    alarmMinute: document.getElementById('alarm-minute'),
    setAlarmBtn: document.getElementById('set-alarm-btn'),
    alarmList: document.getElementById('alarm-list'),
    snoozeInterval: document.getElementById('snooze-interval'),
    testAlarmBtn: document.getElementById('test-alarm-btn'),
    stopAlarmBtn: document.getElementById('stop-alarm-btn'),
    resetDetectorBtn: document.getElementById('reset-detector-btn'),
    detectionThreshold: document.getElementById('detection-threshold'),
    thresholdValue: document.getElementById('threshold-value'),
    logContainer: document.getElementById('log-container'),
    clearLogBtn: document.getElementById('clear-log-btn'),
    connectionStatus: document.getElementById('connection-status')
};

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    initTimeSelectors();
    bindEvents();
    startUpdateLoop();
    loadAlarms();
    addLog('系统初始化完成', 'info');
});

// 初始化时间选择器
function initTimeSelectors() {
    const hourSelect = elements.alarmHour;
    const minuteSelect = elements.alarmMinute;
    
    // 填充小时选项 (0-23)
    for (let i = 0; i < 24; i++) {
        const option = document.createElement('option');
        option.value = i.toString().padStart(2, '0');
        option.textContent = i.toString().padStart(2, '0');
        hourSelect.appendChild(option);
    }
    
    // 填充分钟选项 (0-59)
    for (let i = 0; i < 60; i++) {
        const option = document.createElement('option');
        option.value = i.toString().padStart(2, '0');
        option.textContent = i.toString().padStart(2, '0');
        minuteSelect.appendChild(option);
    }
    
    // 设置默认值为当前时间的下一个整点
    const now = new Date();
    hourSelect.value = now.getHours().toString().padStart(2, '0');
    minuteSelect.value = '00';
}

// 绑定事件
function bindEvents() {
    // 设置闹钟按钮
    elements.setAlarmBtn.addEventListener('click', setAlarm);
    
    // 测试响铃按钮
    elements.testAlarmBtn.addEventListener('click', () => {
        testAlarm();
        addLog('测试响铃已触发', 'info');
    });
    
    // 停止响铃按钮
    elements.stopAlarmBtn.addEventListener('click', () => {
        stopAlarm();
        addLog('响铃已停止', 'info');
    });
    
    // 重置检测器按钮
    elements.resetDetectorBtn.addEventListener('click', () => {
        resetDetector();
        addLog('检测器已重置', 'info');
    });
    
    // 阈值滑块
    elements.detectionThreshold.addEventListener('input', (e) => {
        elements.thresholdValue.textContent = e.target.value;
    });
    
    elements.detectionThreshold.addEventListener('change', (e) => {
        setThreshold(e.target.value);
        addLog(`检测阈值已设置为: ${e.target.value}`, 'info');
    });
    
    // 清空日志按钮
    elements.clearLogBtn.addEventListener('click', () => {
        logEntries = [];
        renderLogs();
    });
}

// API请求封装
async function apiRequest(endpoint, method = 'GET', data = null) {
    try {
        const options = {
            method: method,
            headers: {
                'Content-Type': 'application/json'
            }
        };
        
        if (data) {
            options.body = JSON.stringify(data);
        }
        
        const response = await fetch(`${API_BASE}${endpoint}`, options);
        
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        
        return await response.json();
    } catch (error) {
        console.error('API request failed:', error);
        systemStatus.isConnected = false;
        updateConnectionStatus();
        throw error;
    }
}

// 获取系统状态
async function fetchStatus() {
    try {
        const data = await apiRequest('/status');
        systemStatus = {
            isPersonPresent: data.is_person_present,
            confidence: data.confidence,
            nextAlarm: data.next_alarm,
            wifiSignal: data.wifi_signal,
            isConnected: true
        };
        updateUI();
    } catch (error) {
        console.error('Failed to fetch status:', error);
    }
}

// 更新UI
function updateUI() {
    updateConnectionStatus();
    
    // 更新检测状态
    if (systemStatus.isPersonPresent) {
        elements.detectionStatus.textContent = '有人';
        elements.detectionStatus.className = 'value detected';
    } else {
        elements.detectionStatus.textContent = '无人';
        elements.detectionStatus.className = 'value not-detected';
    }
    
    // 更新置信度
    elements.confidence.textContent = `${(systemStatus.confidence * 100).toFixed(1)}%`;
    
    // 更新下次响铃时间
    if (systemStatus.nextAlarm) {
        elements.nextAlarm.textContent = systemStatus.nextAlarm;
    } else {
        elements.nextAlarm.textContent = '--:--';
    }
    
    // 更新WiFi信号
    elements.wifiSignal.textContent = `${systemStatus.wifiSignal} dBm`;
}

// 更新连接状态
function updateConnectionStatus() {
    const status = elements.connectionStatus;
    if (systemStatus.isConnected) {
        status.textContent = '✓ 已连接';
        status.className = 'connection-status connected';
    } else {
        status.textContent = '⚠ 未连接';
        status.className = 'connection-status disconnected';
    }
}

// 加载闹钟列表
async function loadAlarms() {
    try {
        const data = await apiRequest('/alarms');
        alarms = data.alarms || [];
        renderAlarms();
    } catch (error) {
        console.error('Failed to load alarms:', error);
    }
}

// 设置闹钟
async function setAlarm() {
    const hour = elements.alarmHour.value;
    const minute = elements.alarmMinute.value;
    const timeStr = `${hour}:${minute}`;
    
    try {
        await apiRequest('/alarms', 'POST', {
            time: timeStr,
            enabled: true
        });
        
        addLog(`闹钟已设置: ${timeStr}`, 'success');
        loadAlarms();
    } catch (error) {
        addLog('设置闹钟失败', 'error');
    }
}

// 删除闹钟
async function deleteAlarm(id) {
    try {
        await apiRequest(`/alarms/${id}`, 'DELETE');
        addLog('闹钟已删除', 'info');
        loadAlarms();
    } catch (error) {
        addLog('删除闹钟失败', 'error');
    }
}

// 启用/禁用闹钟
async function toggleAlarm(id, enabled) {
    try {
        await apiRequest(`/alarms/${id}`, 'PUT', { enabled });
        addLog(`闹钟已${enabled ? '启用' : '禁用'}`, 'info');
        loadAlarms();
    } catch (error) {
        addLog('更新闹钟失败', 'error');
    }
}

// 渲染闹钟列表
function renderAlarms() {
    elements.alarmList.innerHTML = '';
    
    if (alarms.length === 0) {
        elements.alarmList.innerHTML = '<li class="empty">暂无闹钟</li>';
        return;
    }
    
    alarms.forEach(alarm => {
        const li = document.createElement('li');
        li.className = alarm.enabled ? 'active' : '';
        if (alarm.is_ringing) {
            li.className += ' ringing';
        }
        
        li.innerHTML = `
            <span class="alarm-time">${alarm.time}</span>
            <div class="alarm-actions">
                <button class="btn btn-small toggle-btn">${alarm.enabled ? '禁用' : '启用'}</button>
                <button class="btn btn-small btn-danger delete-btn">删除</button>
            </div>
        `;
        
        // 绑定事件
        li.querySelector('.toggle-btn').addEventListener('click', () => {
            toggleAlarm(alarm.id, !alarm.enabled);
        });
        
        li.querySelector('.delete-btn').addEventListener('click', () => {
            deleteAlarm(alarm.id);
        });
        
        elements.alarmList.appendChild(li);
    });
}

// 测试响铃
async function testAlarm() {
    try {
        await apiRequest('/test-alarm', 'POST');
    } catch (error) {
        console.error('Failed to test alarm:', error);
    }
}

// 停止响铃
async function stopAlarm() {
    try {
        await apiRequest('/stop-alarm', 'POST');
        addLog('响铃已停止', 'info');
    } catch (error) {
        console.error('Failed to stop alarm:', error);
    }
}

// 重置检测器
async function resetDetector() {
    try {
        await apiRequest('/reset-detector', 'POST');
    } catch (error) {
        console.error('Failed to reset detector:', error);
    }
}

// 设置阈值
async function setThreshold(value) {
    try {
        await apiRequest('/threshold', 'PUT', { threshold: parseInt(value) });
    } catch (error) {
        console.error('Failed to set threshold:', error);
    }
}

// 设置贪睡间隔
async function setSnoozeInterval(interval) {
    try {
        await apiRequest('/snooze-interval', 'PUT', { interval: parseInt(interval) });
        addLog(`贪睡间隔已设置为: ${interval}秒`, 'info');
    } catch (error) {
        console.error('Failed to set snooze interval:', error);
    }
}

// 添加日志
function addLog(message, type = 'info') {
    const timestamp = new Date().toLocaleTimeString('zh-CN');
    logEntries.unshift({
        time: timestamp,
        message: message,
        type: type
    });
    
    // 限制日志数量
    if (logEntries.length > 100) {
        logEntries = logEntries.slice(0, 100);
    }
    
    renderLogs();
}

// 渲染日志
function renderLogs() {
    elements.logContainer.innerHTML = '';
    
    if (logEntries.length === 0) {
        elements.logContainer.innerHTML = '<div class="log-entry">暂无日志</div>';
        return;
    }
    
    logEntries.forEach(entry => {
        const div = document.createElement('div');
        div.className = `log-entry ${entry.type}`;
        div.textContent = `[${entry.time}] ${entry.message}`;
        elements.logContainer.appendChild(div);
    });
    
    // 滚动到底部
    elements.logContainer.scrollTop = elements.logContainer.scrollHeight;
}

// 启动更新循环
function startUpdateLoop() {
    // 立即获取一次状态
    fetchStatus();
    
    // 每2秒更新一次
    updateInterval = setInterval(() => {
        fetchStatus();
    }, 2000);
}

// 停止更新循环
function stopUpdateLoop() {
    if (updateInterval) {
        clearInterval(updateInterval);
        updateInterval = null;
    }
}

// 页面卸载时清理
window.addEventListener('beforeunload', () => {
    stopUpdateLoop();
});

// 贪睡间隔变化事件
elements.snoozeInterval.addEventListener('change', (e) => {
    setSnoozeInterval(e.target.value);
});
