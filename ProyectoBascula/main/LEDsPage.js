// ===== Estado UI =====
let ultimoGramos = null;
let ultimaVBateria = null;

// ===== Conversión de unidades =====
function gTo(unit, g) {
  switch (unit) {
    case 'kg': return g / 1000;
    case 'lb': return g * 0.00220462262;
    default:   return g;
  }
}

function render() {
  const unitSel = document.getElementById('unidad').value;
  const dec = parseInt(document.getElementById('decimales').value, 10);
  const out = document.getElementById('val');
  const unitLbl = document.getElementById('unit');

  if (ultimoGramos === null) {
    out.textContent = '--';
  } else {
    out.textContent = gTo(unitSel, ultimoGramos).toFixed(dec);
  }
  unitLbl.textContent = unitSel;
}

function renderBateria() {
  const el = document.getElementById('batt-val');
  if (!el) return;
  if (ultimaVBateria === null) {
    el.textContent = '-- V';
  } else {
    el.textContent = ultimaVBateria.toFixed(2) + ' V';
  }
}

// ===== Comunicación con el ESP32 =====
function fetchPeso() {
  fetch('/peso', { cache: 'no-store' })
    .then(r => {
      if (!r.ok) throw new Error('HTTP');
      return r.text();
    })
    .then(txt => {
      const v = parseFloat(txt);
      if (!Number.isFinite(v)) throw new Error('NaN');
      ultimoGramos = v;
      render();
      ok('Lectura actualizada');
      console.log('Peso recibido:', v);
    })
    .catch(err => {
      console.error('Error fetch /peso:', err);
      errStatus('Dispositivo desconectado');
    });

  // Bucle periódico
  setTimeout(fetchPeso, 400);
}

function fetchBateria() {
  fetch('/bateria_vbat', { cache: 'no-store' })
    .then(r => {
      if (!r.ok) throw new Error('HTTP');
      return r.text();
    })
    .then(txt => {
      const v = parseFloat(txt);
      if (!Number.isFinite(v)) throw new Error('NaN');
      ultimaVBateria = v;
      renderBateria();
      console.log('Vbat:', v);
    })
    .catch(err => {
      console.error('Error fetch /bateria_vbat:', err);
      ultimaVBateria = null;
      renderBateria();
    });
}

// ===== Mensajes de estado =====
function setStatus(msg, cls) {
  const el = document.getElementById('status');
  el.textContent = msg;
  el.className = 'status ' + cls;
}

function ok(msg)  { setStatus(msg, 'ok'); }
function errStatus(msg) { setStatus(msg, 'err'); }

// ===== Inicio =====
window.addEventListener('load', () => {
  console.log('Página cargada, iniciando bucles de lectura');

  // Eventos para redibujar cuando cambias unidad/decimales
  document.getElementById('unidad').addEventListener('change', render);
  document.getElementById('decimales').addEventListener('change', render);

  render();
  renderBateria();

  fetchPeso();                  // esto se relanza solo con setTimeout
  setInterval(fetchBateria, 5000);  // batería cada 5 s
});

function hacerTara() {
  fetch('/Tara', { cache: 'no-store' })
    .then(() => ok('Tara realizada'))
    .catch(() => errStatus('No se pudo realizar la tara'));
}
