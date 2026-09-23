// ============================================
// 配置
// ============================================
//const API_BASE = 'http://192.168.246.128:8080';
const API_BASE = window.location.origin;
let currentUser = '';
let targetUser = '';
let pollInterval = null;

// ============================================
// DOM 引用
// ============================================
const $ = id => document.getElementById(id);
const loginArea = $('login-area');
const chatArea = $('chat-area');
const usernameInput = $('login-username');
const passwordInput = $('login-password');
const loginError = $('login-error');
const serverStatus = $('server-status');
const currentUserDisplay = $('current-user');
const messagesDiv = $('messages');
const msgInput = $('msg-input');
const toInput = $('to-input');
const onlineUsersList = $('online-users');

// ============================================
// API 调用
// ============================================
async function apiCall(path, data) {
    try {
        const params = new URLSearchParams();
        for (const key in data) {
            params.append(key, data[key]);
        }
        
        const response = await fetch(API_BASE + path + '?' + params.toString(), {
            method: 'GET'
        });
        return await response.json();
    } catch (e) {
        console.error('API 错误:', e);
        return { success: false, message: '网络错误: ' + e.message };
    }
}

// ============================================
// 登录/注册
// ============================================
async function doRegister() {
    const username = usernameInput.value.trim();
    const password = passwordInput.value.trim();
    
    if (!username || !password) {
        loginError.textContent = '请输入用户名和密码';
        return;
    }
    
    const result = await apiCall('/register', { username, password });
    loginError.textContent = result.message;
    loginError.style.color = result.success ? '#2ecc71' : '#ff4757';
    
    if (result.success) {
        setTimeout(() => loginError.textContent = '', 2000);
    }
}

async function doLogin() {
    const username = usernameInput.value.trim();
    const password = passwordInput.value.trim();
    
    if (!username || !password) {
        loginError.textContent = '请输入用户名和密码';
        return;
    }
    
    const result = await apiCall('/login', { username, password });
    
    if (result.success) {
        currentUser = username;
        currentUserDisplay.textContent = username;
        loginArea.style.display = 'none';
        chatArea.style.display = 'block';
        loginError.textContent = '';
        
        // 加载历史消息
        await loadHistory();
        // 开始轮询新消息
        startPolling();
        // 加载在线用户
        await loadOnlineUsers();
        
        serverStatus.textContent = '✅ 已连接';
        serverStatus.style.color = '#2ecc71';
    } else {
        loginError.textContent = result.message;
        loginError.style.color = '#ff4757';
    }
}

// ============================================
// 聊天功能
// ============================================
async function sendMessage() {
    const content = msgInput.value.trim();
    const to = toInput.value.trim() || currentUser;
    
    if (!content) return;
    
    const result = await apiCall('/chat', {
        from: currentUser,
        to: to,
        content: content
    });
    
    if (result.success) {
        msgInput.value = '';
        // 自己发送的消息直接显示
        addMessage('我', content, new Date().toLocaleTimeString(), true);
    } else {
        alert('发送失败: ' + (result.message || '未知错误'));
    }
}

async function loadHistory() {
    const result = await apiCall('/history', {
        username: currentUser,
        limit: 50
    });
    
    if (result.success) {
        messagesDiv.innerHTML = '';
        // 倒序显示（最新的在下面）
        const msgs = result.messages || [];
        for (const msg of msgs.reverse()) {
            const isSelf = msg.from === currentUser;
            addMessage(msg.from, msg.content, msg.time, isSelf);
        }
    }
}

async function loadOnlineUsers() {
    // 简化版：使用 Redis 获取在线用户
    // 实际可以通过 /users 接口
    const result = await apiCall('/users', {});
    // 这里简化处理
}

// ============================================
// 轮询新消息
// ============================================
function startPolling() {
    if (pollInterval) clearInterval(pollInterval);
    pollInterval = setInterval(async () => {
        const result = await apiCall('/poll', { username: currentUser });
        if (result.success && result.messages) {
            for (const msg of result.messages) {
                addMessage(msg.from, msg.content, msg.time, false);
            }
        }
    }, 2000);
}

// ============================================
// UI 更新
// ============================================
function addMessage(from, content, time, isSelf) {
    const div = document.createElement('div');
    div.className = `message ${isSelf ? 'self' : 'other'}`;
    div.innerHTML = `
        <div class="content"><strong>${from}</strong>: ${content}</div>
        <div class="meta"><span class="time">${time || new Date().toLocaleTimeString()}</span></div>
    `;
    messagesDiv.appendChild(div);
    messagesDiv.scrollTop = messagesDiv.scrollHeight;
}

function updateOnlineUsers(users) {
    onlineUsersList.innerHTML = '';
    if (!users || users.length === 0) {
        onlineUsersList.innerHTML = '<li style="color:#999;">暂无在线用户</li>';
        return;
    }
    for (const user of users) {
        const li = document.createElement('li');
        li.className = 'online';
        li.textContent = user;
        li.onclick = () => { toInput.value = user; };
        onlineUsersList.appendChild(li);
    }
}

// ============================================
// 退出
// ============================================
async function doLogout() {
    if (pollInterval) clearInterval(pollInterval);
    // 可选：通知服务器退出
    loginArea.style.display = 'block';
    chatArea.style.display = 'none';
    currentUser = '';
    serverStatus.textContent = '⏳ 已断开';
    serverStatus.style.color = '#999';
}

// ============================================
// 初始化
// ============================================
serverStatus.textContent = '✅ 就绪';
serverStatus.style.color = '#2ecc71';

// 回车键触发登录
passwordInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') doLogin();
});
usernameInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') doLogin();
});

console.log('💬 聊天室已加载');
console.log('API地址:', API_BASE);
