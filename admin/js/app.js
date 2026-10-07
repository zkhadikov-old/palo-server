(function () {
  'use strict';

  var state = {
    baseUrl: 'http://127.0.0.1:7777',
    username: '',
    client: null,
    database: null,
    dimension: null,
    cube: null,
    detailMode: null,
    lastError: '',
    lastOk: '',
    /** Lignes brutes `list/elements` pour le panneau consolidation (exclusion sans requête). */
    lastElementRows: null,
    /** Id d’élément à exclure des cases à cocher (nœud cliqué dans l’arbre). */
    excludeFromConsolidationPick: null,
    /** Nœud parent choisi dans l’arbre pour /element/append (même élément, devient consolidé si besoin). */
    consolidationAnchorElement: null,
    /** Membre du groupe « admin » (voir /server/user_info). */
    isAdmin: false,
    /**
     * Si vrai (et isAdmin) : listes dimensions/cubes avec objets système / attributs / info (flags API show_*).
     * Sinon les listes se comportent comme pour un utilisateur standard (dimensions et cubes « normaux » uniquement).
     */
    adminFullListView: false,
    /** Cache résolution base System + #_USER_ / #_USER_GROUP (invalidé au logout). */
    serverUsersCtx: null,
    /** Dimension Palo `#_Nom_` (type attributs) quand une dimension métier est sélectionnée et résolue. */
    attributeDimension: null,
  };

  var SYSTEM_DB_NAME = 'System';
  var USER_DIM_NAME = '#_USER_';
  var GROUP_DIM_NAME = '#_GROUP_';
  var USER_GROUP_CUBE_NAME = '#_USER_GROUP';
  /** Palo : dimension d’attributs pour une dimension métier « Nom » → `#_Nom_` (voir AttributedDimension). */
  function attributeDimensionNameForBase(normalDimName) {
    return '#_' + String(normalDimName) + '_';
  }

  /** Params pour retrouver la dimension d’attributs (show_attribute requis côté API). */
  function dimensionParamsForAttributeLookup() {
    return {
      show_normal: '1',
      show_attribute: '1',
    };
  }

  /** Comptes créés par défaut dans System (minuscules). */
  var BUILTIN_SERVER_USERS = { admin: 1, _internal_suite: 1, poweruser: 1, editor: 1, viewer: 1 };

  function $(id) {
    return document.getElementById(id);
  }

  /**
   * /element/append — utilise PaloClient.elementAppend si présent (api.js à jour),
   * sinon _fetch (évite l’erreur si le navigateur sert un vieux api.js en cache).
   */
  function elementAppendRequest(client, databaseId, dimensionId, parentElementId, childrenCsv, weightsCsv) {
    if (client && typeof client.elementAppend === 'function') {
      return client.elementAppend(databaseId, dimensionId, parentElementId, childrenCsv, weightsCsv);
    }
    if (!client || typeof client._fetch !== 'function') {
      return Promise.reject(new Error('Client API indisponible. Rechargez la page.'));
    }
    var params = {
      database: databaseId,
      dimension: dimensionId,
      element: parentElementId,
      children: childrenCsv,
    };
    if (weightsCsv) params.weights = weightsCsv;
    return client._fetch('/element/append', params).then(function () {
      return undefined;
    });
  }

  function destroyElementRequest(client, databaseId, dimensionId, elementId) {
    if (client && typeof client.destroyElement === 'function') {
      return client.destroyElement(databaseId, dimensionId, elementId);
    }
    if (!client || typeof client._fetch !== 'function') {
      return Promise.reject(new Error('Client API indisponible. Rechargez la page.'));
    }
    return client._fetch('/element/destroy', {
      database: databaseId,
      dimension: dimensionId,
      element: elementId,
    });
  }

  /** Fait défiler la zone horizontale (#app) pour rendre une colonne visible. */
  function revealColumn(el) {
    if (!el) return;
    requestAnimationFrame(function () {
      el.scrollIntoView({ behavior: 'smooth', inline: 'nearest', block: 'nearest' });
    });
  }

  function hideModal(modalId) {
    var el = $(modalId);
    if (!el || typeof bootstrap === 'undefined') return;
    var m = bootstrap.Modal.getInstance(el);
    if (m) m.hide();
  }

  function updateToolbarButtons() {
    var btnEl = $('btn-open-modal-element');
    var btnAttr = $('btn-open-modal-attribute');
    var btnRule = $('btn-open-modal-rule');
    var btnCons = $('btn-consolidate-create');
    var dimElOk = state.dimension && isDimensionElementsMutable(state.dimension);
    var attrOk =
      state.attributeDimension &&
      isDimensionElementsMutable(state.attributeDimension) &&
      state.dimension &&
      isNormalDimensionRow(state.dimension);
    var dimConsOk = state.dimension && isNormalDimensionRow(state.dimension);
    var cubeOk = state.cube && isNormalCubeRow(state.cube);
    if (btnEl) btnEl.disabled = !dimElOk;
    if (btnAttr) btnAttr.disabled = !attrOk;
    if (btnRule) btnRule.disabled = !cubeOk;
    if (btnCons) btnCons.disabled = !dimConsOk;
  }

  function populateCubeDimensionPicker() {
    var container = $('cube-dim-picker');
    if (!container || !state.client || !state.database) return;
    container.innerHTML = '<div class="list-group-item text-muted small border-0">Chargement…</div>';
    state.client
      .listDimensions(state.database.database)
      .then(function (dims) {
        var normal = dims.filter(function (d) {
          return String(d.type) === '0';
        });
        normal.sort(function (a, b) {
          return String(a.name_dimension).localeCompare(String(b.name_dimension), 'fr', { sensitivity: 'base' });
        });
        container.innerHTML = '';
        normal.forEach(function (d) {
          var btn = document.createElement('button');
          btn.type = 'button';
          btn.className = 'list-group-item list-group-item-action cube-dim-pick-item text-start';
          btn.dataset.dimId = String(d.dimension);
          btn.setAttribute('aria-pressed', 'false');
          btn.textContent = d.name_dimension + ' (id ' + d.dimension + ')';
          btn.addEventListener('click', function () {
            var on = btn.classList.toggle('active');
            btn.setAttribute('aria-pressed', on ? 'true' : 'false');
          });
          container.appendChild(btn);
        });
        if (!normal.length) {
          container.innerHTML =
            '<div class="list-group-item text-muted small border-0">Aucune dimension disponible. Créez une dimension d’abord.</div>';
        }
      })
      .catch(function (e) {
        container.innerHTML =
          '<div class="list-group-item text-danger small border-0">' + escapeHtml(e.message || String(e)) + '</div>';
      });
  }

  function getSelectedCubeDimensionIds() {
    var container = $('cube-dim-picker');
    if (!container) return [];
    var ids = [];
    container.querySelectorAll('.cube-dim-pick-item.active').forEach(function (btn) {
      if (btn.dataset.dimId != null) ids.push(btn.dataset.dimId);
    });
    return ids;
  }

  /** Listes étendues (système, attributs…) : droits admin + option utilisateur. */
  function useExtendedLists() {
    return state.isAdmin && state.adminFullListView;
  }

  /**
   * Admin en mode listes « simples » : dimensions/cubes normaux + attributs côté API (show_attribute),
   * pour pouvoir résoudre #_NomDim_ et afficher les attributs dans le panneau détail.
   * La colonne latérale masque les lignes #_… (voir isDimensionRowInSimpleView / isCubeRowInSimpleView).
   */
  function dbListParamsAdminSimple() {
    if (!state.isAdmin) return null;
    return {
      show_normal: '1',
      show_attribute: '1',
    };
  }

  /** Paramètres API pour tout voir : système, user info, GPU (case « Listes complètes »). */
  function dbListExtraParams() {
    if (!useExtendedLists()) return null;
    return {
      show_normal: '1',
      show_system: '1',
      show_attribute: '1',
      show_info: '1',
      show_gputype: '1',
    };
  }

  /** Params pour /database/dimensions et /database/cubes selon le mode. */
  function dbListParamsForExplorer() {
    if (useExtendedLists()) return dbListExtraParams();
    return dbListParamsAdminSimple();
  }

  /**
   * Vue non étendue : uniquement dimensions « métier » (type 0), sans lignes #_…
   * (les attributs sont affichés dans le panneau détail sous l’arbre).
   */
  function isDimensionRowInSimpleView(d) {
    var t = String(d.type);
    if (t !== '0') return false;
    var n = String(d.name_dimension || '');
    return n.indexOf('#_') !== 0;
  }

  /** Vue non étendue : cubes métier (type 0) sans préfixe #_ (cubes d’attributs exclus). */
  function isCubeRowInSimpleView(c) {
    var t = String(c.type);
    if (t !== '0') return false;
    var n = String(c.name_cube || '');
    return n.indexOf('#_') !== 0;
  }

  /** Résolution des noms de dimensions dans le panneau « Dimensions du cube » : toujours étendu si compte admin. */
  function dimensionListParamsForCubeInfo() {
    if (!state.isAdmin) return null;
    return {
      show_normal: '1',
      show_system: '1',
      show_attribute: '1',
      show_info: '1',
      show_gputype: '1',
    };
  }

  /** Suffixe de type : en vue simple admin, on indique quand même attr. / info pour les lignes affichées. */
  function dimensionTypeSuffix(type) {
    var ts = String(type);
    if (ts === '0' || ts === '1') return '';
    var m = { '2': 'attr.', '3': 'info' };
    var s = m[ts];
    if (!s) return '';
    if (useExtendedLists()) return ' · ' + s;
    if (state.isAdmin && (ts === '2' || ts === '3')) return ' · ' + s;
    return '';
  }

  function cubeTypeSuffix(type) {
    var ts = String(type);
    if (ts === '0' || ts === '1') return '';
    var m = { '2': 'attr.', '3': 'info', '4': 'GPU' };
    var s = m[ts];
    if (!s) return '';
    if (useExtendedLists()) return ' · ' + s;
    if (state.isAdmin && (ts === '2' || ts === '3')) return ' · ' + s;
    return '';
  }

  /**
   * Type Palo CSV dimension : 0 = NORMAL ; 2 = attributs (#_Nom_#_) ; 1 = système ; 3 = user info ; 4 = virtuel.
   * Seules les dimensions normales (0) sont supprimables comme dimension « métier ».
   */
  function isNormalDimensionRow(d) {
    return d != null && String(d.type) === '0';
  }

  /** Création / suppression d’éléments : dimensions normales (0) ou d’attributs (2). */
  function isDimensionElementsMutable(d) {
    if (!d) return false;
    var t = String(d.type);
    return t === '0' || t === '2';
  }

  /** 0 = cube normal ; les cubes système / attributs / info / GPU ne sont pas supprimables comme un cube métier. */
  function isNormalCubeRow(c) {
    return c != null && String(c.type) === '0';
  }

  function parseCubeDimensionIds(dimensionsField) {
    if (dimensionsField == null || dimensionsField === '') return [];
    return String(dimensionsField)
      .split(',')
      .map(function (x) {
        return x.trim();
      })
      .filter(Boolean);
  }

  /** Affiche les dimensions du cube sélectionné (liste informative, non interactive). */
  function refreshCubeDimensionInfo() {
    var ul = $('cube-view-dim-list');
    if (!ul || !state.client || !state.database || !state.cube) return Promise.resolve();
    var ids = parseCubeDimensionIds(state.cube.dimensions);
    if (!ids.length) {
      ul.innerHTML =
        '<li class="list-group-item small text-muted border-0">Aucune dimension référencée pour ce cube.</li>';
      return Promise.resolve();
    }
    var extra = dimensionListParamsForCubeInfo();
    return state.client
      .listDimensions(state.database.database, extra || undefined)
      .then(function (dims) {
        var idToName = {};
        dims.forEach(function (d) {
          idToName[String(d.dimension)] = d.name_dimension;
        });
        ul.innerHTML = '';
        ids.forEach(function (id, idx) {
          var li = el('li', 'list-group-item small py-2');
          li.textContent = idx + 1 + '. ' + (idToName[id] || '?') + ' (id ' + id + ')';
          ul.appendChild(li);
        });
      })
      .catch(function (e) {
        ul.innerHTML =
          '<li class="list-group-item small text-danger border-0">' + escapeHtml(e.message || String(e)) + '</li>';
      });
  }

  function escapeHtml(s) {
    var d = document.createElement('div');
    d.textContent = s == null ? '' : String(s);
    return d.innerHTML;
  }

  function showError(msg) {
    state.lastError = msg || '';
    state.lastOk = '';
    var el = $('global-error');
    if (el) {
      el.textContent = state.lastError;
      el.classList.toggle('d-none', !state.lastError);
    }
  }

  function showOk(msg) {
    state.lastOk = msg || '';
    state.lastError = '';
    var el = $('global-ok');
    if (el) {
      el.textContent = state.lastOk;
      el.classList.toggle('d-none', !state.lastOk);
    }
  }

  function clearMessages() {
    showError('');
    showOk('');
  }

  function el(tag, className, text) {
    var n = document.createElement(tag);
    if (className) n.className = className;
    if (text != null) n.textContent = text;
    return n;
  }

  function isProtectedServerUser(name) {
    return !!BUILTIN_SERVER_USERS[String(name).toLowerCase()];
  }

  function serverExplorerListParams() {
    return { show_normal: '1', show_system: '1', show_user_info: '1' };
  }

  function clearServerUsersCtx() {
    state.serverUsersCtx = null;
  }

  function resolveServerUsersContext() {
    if (!state.client) return Promise.reject(new Error('Non connecté.'));
    if (state.serverUsersCtx) return Promise.resolve(state.serverUsersCtx);
    var ep = serverExplorerListParams();
    return state.client.listDatabases(ep).then(function (dbs) {
      var sys = dbs.filter(function (d) {
        return String(d.name_database) === SYSTEM_DB_NAME;
      })[0];
      if (!sys) {
        throw new Error(
          'Base « System » introuvable. Connectez-vous avec un compte du groupe admin pour gérer les utilisateurs.'
        );
      }
      return Promise.all([
        state.client.listDimensions(sys.database, ep),
        state.client.listCubes(sys.database, ep),
      ]).then(function (both) {
        var dims = both[0];
        var cubes = both[1];
        var userDim = dims.filter(function (d) {
          return String(d.name_dimension) === USER_DIM_NAME;
        })[0];
        var groupDim = dims.filter(function (d) {
          return String(d.name_dimension) === GROUP_DIM_NAME;
        })[0];
        var ugCube = cubes.filter(function (c) {
          return String(c.name_cube) === USER_GROUP_CUBE_NAME;
        })[0];
        if (!userDim) throw new Error('Dimension #_USER_ introuvable.');
        if (!groupDim) throw new Error('Dimension #_GROUP_ introuvable.');
        if (!ugCube) throw new Error('Cube #_USER_GROUP introuvable.');
        state.serverUsersCtx = {
          databaseId: sys.database,
          userDimensionId: userDim.dimension,
          groupDimensionId: groupDim.dimension,
          userGroupCubeId: ugCube.cube,
        };
        return state.serverUsersCtx;
      });
    });
  }

  function usersModalSetError(msg) {
    var errEl = $('users-modal-error');
    if (!errEl) return;
    if (msg) {
      errEl.textContent = msg;
      errEl.classList.remove('d-none');
    } else {
      errEl.textContent = '';
      errEl.classList.add('d-none');
    }
  }

  function usersModalSetLoading(on) {
    var elLoad = $('users-modal-loading');
    if (elLoad) elLoad.classList.toggle('d-none', !on);
  }

  function refreshServerUsersList() {
    usersModalSetError('');
    if (!state.client || !state.isAdmin) return Promise.resolve();
    usersModalSetLoading(true);
    var ul = $('users-list');
    if (ul) ul.innerHTML = '';
    return resolveServerUsersContext()
      .then(function (ctx) {
        return state.client.listElements(ctx.databaseId, ctx.userDimensionId);
      })
      .then(function (rows) {
        usersModalSetLoading(false);
        if (!ul) return;
        ul.innerHTML = '';
        rows
          .slice()
          .sort(function (a, b) {
            return String(a.name_element).localeCompare(String(b.name_element), 'fr', { sensitivity: 'base' });
          })
          .forEach(function (r) {
            var li = el('li', 'list-group-item d-flex align-items-center gap-2 py-2');
            var main = el('span', 'flex-grow-1 text-break small');
            main.textContent = r.name_element;
            li.appendChild(main);
            var prot = isProtectedServerUser(r.name_element);
            var selfUser =
              state.username && String(state.username).toLowerCase() === String(r.name_element).toLowerCase();
            if (!prot && !selfUser) {
              var btn = el('button', 'btn btn-sm btn-outline-danger rounded-circle list-row-delete flex-shrink-0');
              btn.type = 'button';
              btn.title = 'Supprimer';
              btn.setAttribute('aria-label', 'Supprimer l’utilisateur');
              btn.textContent = '−';
              btn.addEventListener('click', function () {
                deleteServerUser(r);
              });
              li.appendChild(btn);
            }
            ul.appendChild(li);
          });
      })
      .catch(function (e) {
        usersModalSetLoading(false);
        usersModalSetError(e.message || String(e));
      });
  }

  function deleteServerUser(row) {
    if (!window.confirm('Supprimer l’utilisateur « ' + row.name_element + ' » ?')) return;
    usersModalSetError('');
    resolveServerUsersContext()
      .then(function (ctx) {
        return state.client.destroyElement(ctx.databaseId, ctx.userDimensionId, row.element);
      })
      .then(function () {
        clearServerUsersCtx();
        return refreshServerUsersList();
      })
      .catch(function (e) {
        usersModalSetError(e.message || String(e));
      });
  }

  function assignNewUserViewerGroup(userElementId) {
    if (!state.client.replaceCell) return Promise.resolve();
    return resolveServerUsersContext().then(function (ctx) {
      return state.client.listElements(ctx.databaseId, ctx.groupDimensionId).then(function (gRows) {
        var viewer = gRows.filter(function (g) {
          return String(g.name_element).toLowerCase() === 'viewer';
        })[0];
        if (!viewer) return;
        return state.client.replaceCell(
          ctx.databaseId,
          ctx.userGroupCubeId,
          String(userElementId) + ',' + String(viewer.element),
          '1',
          { splash: '0' }
        );
      });
    });
  }

  /** Palo n’a pas de cube « user/role » : ouvrir la base System pour #_USER_GROUP et #_GROUP_ROLE. */
  function openSystemDatabaseInColumns() {
    if (!state.client) return Promise.resolve();
    usersModalSetError('');
    return state.client.listDatabases(serverExplorerListParams()).then(function (rows) {
      var sys = rows.filter(function (d) {
        return String(d.name_database) === SYSTEM_DB_NAME;
      })[0];
      if (!sys) {
        usersModalSetError('Base « System » introuvable (compte admin requis).');
        return;
      }
      hideModal('modalUsers');
      selectDatabase(sys);
      var appEl = $('app');
      if (appEl) {
        requestAnimationFrame(function () {
          appEl.scrollLeft = appEl.scrollWidth;
        });
      }
    }).catch(function (e) {
      usersModalSetError(e.message || String(e));
    });
  }

  function bindServerUsers() {
    var modal = $('modalUsers');
    if (modal) {
      modal.addEventListener('show.bs.modal', function () {
        if (!state.isAdmin) return;
        clearServerUsersCtx();
        refreshServerUsersList();
      });
    }
    var btnOpenSys = $('btn-open-system-db');
    if (btnOpenSys) {
      btnOpenSys.addEventListener('click', function () {
        openSystemDatabaseInColumns();
      });
    }
    var btnCreate = $('btn-server-user-create');
    if (btnCreate) {
      btnCreate.addEventListener('click', function () {
        usersModalSetError('');
        var inpName = $('inp-new-server-user');
        var inpPass = $('inp-new-server-user-pass');
        var name = (inpName && inpName.value.trim()) || '';
        var pass = (inpPass && inpPass.value) || '';
        if (!name) {
          usersModalSetError('Indiquez un identifiant.');
          return;
        }
        if (!pass) {
          usersModalSetError('Indiquez un mot de passe initial.');
          return;
        }
        if (isProtectedServerUser(name)) {
          usersModalSetError('Cet identifiant est réservé au système.');
          return;
        }
        btnCreate.disabled = true;
        clearServerUsersCtx();
        resolveServerUsersContext()
          .then(function (ctx) {
            return state.client.createElement(ctx.databaseId, ctx.userDimensionId, name, 2, undefined, undefined);
          })
          .then(function (created) {
            if (typeof state.client.changePassword !== 'function') {
              throw new Error('Client API incomplet : rechargez la page (api.js).');
            }
            return state.client.changePassword(name, pass).then(function () {
              return created;
            });
          })
          .then(function (created) {
            return assignNewUserViewerGroup(created.element).catch(function () {
              /* droits / splash : le compte existe quand même, groupe à ajuster dans #_USER_GROUP si besoin */
            });
          })
          .then(function () {
            if (inpName) inpName.value = '';
            if (inpPass) inpPass.value = '';
            clearServerUsersCtx();
            return refreshServerUsersList();
          })
          .catch(function (e) {
            usersModalSetError(e.message || String(e));
          })
          .then(function () {
            btnCreate.disabled = false;
          });
      });
    }
  }

  function openConsolidationPanel(node) {
    if (!isNormalDimensionRow(state.dimension)) {
      var colHide = $('col-consolidate');
      if (colHide) colHide.classList.add('d-none');
      return;
    }
    state.consolidationAnchorElement = node || null;
    state.excludeFromConsolidationPick = node && node.element != null ? String(node.element) : null;
    if (state.lastElementRows) {
      refreshConsolidationPickerFromRows(state.lastElementRows);
    }
    var colCons = $('col-consolidate');
    var ctitle = $('consolidate-title');
    if (ctitle && state.dimension) {
      var suffix = node && node.name_element != null ? ' — ' + node.name_element : '';
      ctitle.textContent = 'Consolidation — ' + state.dimension.name_dimension + suffix;
    }
    if (colCons) {
      colCons.classList.remove('d-none');
      revealColumn(colCons);
    }
  }

  function renderTree(ul, byId, roots) {
    roots.forEach(function (id) {
      var node = byId[id];
      if (!node) return;
      var li = el('li', 'list-group-item py-2');
      li.dataset.elementId = String(node.element);
      var row = el('div', 'd-flex align-items-start gap-1');
      var main = el('div', 'list-group-item-action list-row-main flex-grow-1 min-width-0');
      var line = el('span', 'd-inline-flex flex-wrap align-items-baseline gap-1');
      line.innerHTML =
        '<span class="name">' +
        escapeHtml(node.name_element) +
        '</span>' +
        '<span class="meta text-muted small">(' +
        elementTypeLabel(node.type) +
        ' · id ' +
        escapeHtml(node.element) +
        ')</span>';
      main.appendChild(line);
      main.addEventListener('click', function (ev) {
        ev.stopPropagation();
        document.querySelectorAll('#element-tree li.list-group-item').forEach(function (x) {
          x.classList.remove('active');
        });
        li.classList.add('active');
        openConsolidationPanel(node);
      });
      row.appendChild(main);
      if (isDimensionElementsMutable(state.dimension)) {
        var delEl = el('button', 'btn btn-sm btn-outline-danger rounded-circle list-row-delete flex-shrink-0');
        delEl.type = 'button';
        delEl.setAttribute('aria-label', 'Supprimer l’élément');
        delEl.title = 'Supprimer';
        delEl.textContent = '−';
        delEl.addEventListener('click', function (ev) {
          ev.stopPropagation();
          deleteElementRow(node);
        });
        row.appendChild(delEl);
      }
      li.appendChild(row);
      var childIds = splitIds(node.children);
      if (childIds.length) {
        var sub = el('ul', 'list-group list-group-flush border-0 ms-3 mt-1 mb-0 ps-2 element-tree-nested');
        renderTree(sub, byId, childIds);
        li.appendChild(sub);
      }
      ul.appendChild(li);
    });
  }

  function splitIds(s) {
    if (!s) return [];
    return String(s)
      .split(',')
      .map(function (x) {
        return x.trim();
      })
      .filter(Boolean);
  }

  function elementTypeLabel(t) {
    var m = { '1': 'num', '2': 'str', '4': 'consolidé' };
    return m[String(t)] || String(t);
  }

  function buildElementTree(rows) {
    var byId = {};
    rows.forEach(function (r) {
      byId[String(r.element)] = r;
    });
    var roots = [];
    rows.forEach(function (r) {
      var np = parseInt(r.number_parents, 10);
      if (!np) roots.push(String(r.element));
    });
    roots.sort(function (a, b) {
      return parseInt(byId[a].position, 10) - parseInt(byId[b].position, 10);
    });
    return { byId: byId, roots: roots };
  }

  function deleteDimensionRow(d) {
    clearMessages();
    if (!state.client || !state.database || !isNormalDimensionRow(d)) return;
    if (!window.confirm('Supprimer la dimension « ' + d.name_dimension + ' » ?')) return;
    state.client
      .destroyDimension(state.database.database, d.dimension)
      .then(function () {
        showOk('Dimension supprimée.');
        if (state.dimension && String(state.dimension.dimension) === String(d.dimension)) {
          state.dimension = null;
          state.attributeDimension = null;
          state.lastElementRows = null;
          state.excludeFromConsolidationPick = null;
          state.consolidationAnchorElement = null;
          $('col-detail').classList.add('d-none');
          var colC = $('col-consolidate');
          if (colC) colC.classList.add('d-none');
        }
        updateToolbarButtons();
        return refreshDimensionsAndCubes();
      })
      .catch(function (e) {
        showError(e.message || String(e));
      });
  }

  function deleteCubeRow(c) {
    clearMessages();
    if (!state.client || !state.database || !isNormalCubeRow(c)) return;
    if (!window.confirm('Supprimer le cube « ' + c.name_cube + ' » ?')) return;
    state.client
      .destroyCube(state.database.database, c.cube)
      .then(function () {
        showOk('Cube supprimé.');
        if (state.cube && String(state.cube.cube) === String(c.cube)) {
          state.cube = null;
          $('col-detail').classList.add('d-none');
        }
        updateToolbarButtons();
        return refreshDimensionsAndCubes();
      })
      .catch(function (e) {
        showError(e.message || String(e));
      });
  }

  function deleteAttributeElementRow(row) {
    clearMessages();
    if (!state.client || !state.database || !state.attributeDimension || !isDimensionElementsMutable(state.attributeDimension)) {
      return;
    }
    if (!window.confirm('Supprimer l’attribut « ' + row.name_element + ' » ?')) return;
    destroyElementRequest(state.client, state.database.database, state.attributeDimension.dimension, row.element)
      .then(function () {
        showOk('Attribut supprimé.');
        return refreshAttributePanel();
      })
      .catch(function (e) {
        showError(e.message || String(e));
      });
  }

  function deleteElementRow(node) {
    clearMessages();
    if (!state.client || !state.database || !state.dimension || !isDimensionElementsMutable(state.dimension)) return;
    if (!window.confirm('Supprimer l’élément « ' + node.name_element + ' » ?')) return;
    destroyElementRequest(state.client, state.database.database, state.dimension.dimension, node.element)
      .then(function () {
        showOk('Élément supprimé.');
        if (state.consolidationAnchorElement && String(state.consolidationAnchorElement.element) === String(node.element)) {
          state.consolidationAnchorElement = null;
          state.excludeFromConsolidationPick = null;
          var colC = $('col-consolidate');
          if (colC) colC.classList.add('d-none');
        }
        return refreshElements();
      })
      .catch(function (e) {
        showError(e.message || String(e));
      });
  }

  function deleteRuleRow(r) {
    clearMessages();
    if (!state.client || !state.database || !state.cube || !isNormalCubeRow(state.cube)) return;
    if (!window.confirm('Supprimer cette règle ?')) return;
    state.client
      .destroyRule(state.database.database, state.cube.cube, r.rule)
      .then(function () {
        showOk('Règle supprimée.');
        return refreshRules();
      })
      .catch(function (e) {
        showError(e.message || String(e));
      });
  }

  function refreshDatabases() {
    if (!state.client) return Promise.resolve();
    var dbParams = useExtendedLists() ? serverExplorerListParams() : undefined;
    return state.client.listDatabases(dbParams).then(function (rows) {
      var list = $('db-list');
      list.innerHTML = '';
      rows.forEach(function (db) {
        var li = el('li', 'list-group-item list-group-item-action');
        li.textContent = db.name_database + ' (' + db.database + ')';
        li.dataset.dbId = db.database;
        if (state.database && String(state.database.database) === String(db.database)) {
          li.classList.add('active');
        }
        li.addEventListener('click', function () {
          selectDatabase(db);
        });
        list.appendChild(li);
      });
    });
  }

  function selectDatabase(db) {
    state.database = db;
    state.dimension = null;
    state.cube = null;
    state.attributeDimension = null;
    state.detailMode = null;
    state.lastElementRows = null;
    state.excludeFromConsolidationPick = null;
    state.consolidationAnchorElement = null;
    state.client.clearDbToken();
    document.querySelectorAll('#db-list .list-group-item').forEach(function (x) {
      x.classList.toggle('active', String(x.dataset.dbId) === String(db.database));
    });
    $('col-db-content').classList.remove('d-none');
    $('col-detail').classList.add('d-none');
    var colCons = $('col-consolidate');
    if (colCons) colCons.classList.add('d-none');
    var sysHint = $('db-content-system-hint');
    if (sysHint) {
      sysHint.classList.toggle('d-none', String(db.name_database) !== SYSTEM_DB_NAME);
    }
    updateToolbarButtons();
    return refreshDimensionsAndCubes();
  }

  function refreshDimensionsAndCubes() {
    if (!state.client || !state.database) return Promise.resolve();
    var dbId = state.database.database;
    var extra = dbListParamsForExplorer();
    return Promise.all([
      state.client.listDimensions(dbId, extra || undefined),
      state.client.listCubes(dbId, extra || undefined),
    ]).then(function (both) {
      var dims = both[0];
      var cubes = both[1];
      var dimList = $('dim-list');
      dimList.innerHTML = '';
      dims.forEach(function (d) {
        if (!useExtendedLists() && !isDimensionRowInSimpleView(d)) return;
        var li = el('li', 'list-group-item d-flex align-items-center gap-1 py-2 px-2');
        li.dataset.dimId = d.dimension;
        if (state.dimension && String(state.dimension.dimension) === String(d.dimension)) {
          li.classList.add('active');
        }
        var main = el('div', 'list-group-item-action list-row-main flex-grow-1 text-truncate list-item-icon-dim');
        main.textContent = d.name_dimension + dimensionTypeSuffix(d.type);
        main.addEventListener('click', function (ev) {
          ev.stopPropagation();
          selectDimension(d);
        });
        li.appendChild(main);
        if (isNormalDimensionRow(d)) {
          var delDim = el('button', 'btn btn-sm btn-outline-danger rounded-circle list-row-delete flex-shrink-0');
          delDim.type = 'button';
          delDim.setAttribute('aria-label', 'Supprimer la dimension');
          delDim.title = 'Supprimer';
          delDim.textContent = '−';
          delDim.addEventListener('click', function (ev) {
            ev.stopPropagation();
            deleteDimensionRow(d);
          });
          li.appendChild(delDim);
        }
        dimList.appendChild(li);
      });
      var cubeList = $('cube-list');
      cubeList.innerHTML = '';
      cubes.forEach(function (c) {
        if (!useExtendedLists() && !isCubeRowInSimpleView(c)) return;
        var li = el('li', 'list-group-item d-flex align-items-center gap-1 py-2 px-2');
        li.dataset.cubeId = c.cube;
        if (state.cube && String(state.cube.cube) === String(c.cube)) {
          li.classList.add('active');
        }
        var mainC = el('div', 'list-group-item-action list-row-main flex-grow-1 text-truncate list-item-icon-cube');
        mainC.textContent = c.name_cube + cubeTypeSuffix(c.type);
        mainC.addEventListener('click', function (ev) {
          ev.stopPropagation();
          selectCube(c);
        });
        li.appendChild(mainC);
        if (isNormalCubeRow(c)) {
          var delCube = el('button', 'btn btn-sm btn-outline-danger rounded-circle list-row-delete flex-shrink-0');
          delCube.type = 'button';
          delCube.setAttribute('aria-label', 'Supprimer le cube');
          delCube.title = 'Supprimer';
          delCube.textContent = '−';
          delCube.addEventListener('click', function (ev) {
            ev.stopPropagation();
            deleteCubeRow(c);
          });
          li.appendChild(delCube);
        }
        cubeList.appendChild(li);
      });
    });
  }

  function selectDimension(d) {
    state.dimension = d;
    state.cube = null;
    state.detailMode = 'elements';
    state.excludeFromConsolidationPick = null;
    state.consolidationAnchorElement = null;
    state.lastElementRows = null;
    document.querySelectorAll('#dim-list .list-group-item').forEach(function (x) {
      x.classList.toggle('active', String(x.dataset.dimId) === String(d.dimension));
    });
    document.querySelectorAll('#cube-list .list-group-item').forEach(function (x) {
      x.classList.remove('active');
    });
    $('col-detail').classList.remove('d-none');
    $('detail-title').textContent = 'Éléments — ' + d.name_dimension;
    var colCons = $('col-consolidate');
    if (colCons) colCons.classList.add('d-none');
    $('detail-elements').classList.remove('d-none');
    $('detail-rules').classList.add('d-none');
    updateToolbarButtons();
    return refreshElements().then(function () {
      return refreshAttributePanel();
    });
  }

  /** Liste des noms d’attributs (éléments de `#_Dimension_`) pour une dimension métier sélectionnée. */
  function refreshAttributePanel() {
    var wrap = $('detail-attributes');
    var ul = $('attr-element-list');
    var noneEl = $('attr-panel-none');
    var errEl = $('attr-panel-error');
    var nameLabel = $('attr-dim-name-label');
    state.attributeDimension = null;
    updateToolbarButtons();
    if (errEl) {
      errEl.textContent = '';
      errEl.classList.add('d-none');
    }
    if (!wrap || !ul) return Promise.resolve();
    if (!state.client || !state.database || !state.dimension || !isNormalDimensionRow(state.dimension)) {
      wrap.classList.add('d-none');
      return Promise.resolve();
    }
    wrap.classList.remove('d-none');
    var attrDimName = attributeDimensionNameForBase(state.dimension.name_dimension);
    if (nameLabel) nameLabel.textContent = attrDimName;
    ul.innerHTML = '<li class="list-group-item small text-muted border-0 py-2">Chargement…</li>';
    if (noneEl) noneEl.classList.add('d-none');
    return state.client
      .listDimensions(state.database.database, dimensionParamsForAttributeLookup())
      .then(function (dims) {
        var attrDim = dims.filter(function (x) {
          return String(x.name_dimension) === attrDimName;
        })[0];
        if (!attrDim) {
          state.attributeDimension = null;
          ul.innerHTML = '';
          if (noneEl) noneEl.classList.remove('d-none');
          updateToolbarButtons();
          return;
        }
        state.attributeDimension = attrDim;
        if (noneEl) noneEl.classList.add('d-none');
        return state.client.listElements(state.database.database, attrDim.dimension).then(function (rows) {
          ul.innerHTML = '';
          var mutable = isDimensionElementsMutable(attrDim);
          rows
            .slice()
            .sort(function (a, b) {
              return parseInt(a.position, 10) - parseInt(b.position, 10);
            })
            .forEach(function (r) {
              var li = el('li', 'list-group-item d-flex align-items-center gap-1 py-2 px-2');
              var main = el(
                'div',
                'list-row-main flex-grow-1 min-width-0 text-truncate small'
              );
              var line = el('span', 'd-inline-flex flex-wrap align-items-baseline gap-1');
              line.innerHTML =
                '<span class="name">' +
                escapeHtml(r.name_element) +
                '</span>' +
                '<span class="meta text-muted small">(id ' +
                escapeHtml(r.element) +
                ')</span>';
              main.appendChild(line);
              li.appendChild(main);
              if (mutable) {
                var delBtn = el('button', 'btn btn-sm btn-outline-danger rounded-circle list-row-delete flex-shrink-0');
                delBtn.type = 'button';
                delBtn.setAttribute('aria-label', 'Supprimer l’attribut');
                delBtn.title = 'Supprimer';
                delBtn.textContent = '−';
                delBtn.addEventListener('click', function (ev) {
                  ev.stopPropagation();
                  deleteAttributeElementRow(r);
                });
                li.appendChild(delBtn);
              }
              ul.appendChild(li);
            });
          if (!rows.length) {
            var emptyLi = el('li', 'list-group-item small text-muted border-0 py-2');
            emptyLi.textContent = 'Aucun nom d’attribut défini. Utilisez le bouton + ci-dessus.';
            ul.appendChild(emptyLi);
          }
          updateToolbarButtons();
        });
      })
      .catch(function (e) {
        state.attributeDimension = null;
        ul.innerHTML = '';
        updateToolbarButtons();
        if (errEl) {
          errEl.textContent = e.message || String(e);
          errEl.classList.remove('d-none');
        }
      });
  }

  function selectCube(c) {
    state.cube = c;
    state.dimension = null;
    state.attributeDimension = null;
    state.detailMode = 'rules';
    state.lastElementRows = null;
    state.excludeFromConsolidationPick = null;
    state.consolidationAnchorElement = null;
    document.querySelectorAll('#cube-list .list-group-item').forEach(function (x) {
      x.classList.toggle('active', String(x.dataset.cubeId) === String(c.cube));
    });
    document.querySelectorAll('#dim-list .list-group-item').forEach(function (x) {
      x.classList.remove('active');
    });
    $('col-detail').classList.remove('d-none');
    $('detail-title').textContent = 'Règles — ' + c.name_cube;
    var colCons = $('col-consolidate');
    if (colCons) colCons.classList.add('d-none');
    $('detail-elements').classList.add('d-none');
    $('detail-rules').classList.remove('d-none');
    updateToolbarButtons();
    return refreshCubeDimensionInfo().then(function () {
      return refreshRules();
    });
  }

  function refreshConsolidationPickerFromRows(rows) {
    var container = $('consolidate-pick-list');
    if (!container) return;
    var basesAll = rows.filter(function (r) {
      var t = String(r.type);
      return t === '1' || t === '2';
    });
    basesAll.sort(function (a, b) {
      return parseInt(a.position, 10) - parseInt(b.position, 10);
    });
    var ex = state.excludeFromConsolidationPick;
    var bases = basesAll.filter(function (r) {
      return !ex || String(r.element) !== String(ex);
    });
    container.innerHTML = '';
    bases.forEach(function (r) {
      var label = document.createElement('label');
      label.className = 'list-group-item d-flex align-items-center gap-2 mb-0 py-2';
      var cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.className = 'form-check-input flex-shrink-0 consolidate-pick';
      cb.value = String(r.element);
      cb.dataset.dimId = String(r.element);
      var span = document.createElement('span');
      span.className = 'small flex-grow-1';
      span.textContent = r.name_element + ' (id ' + r.element + ') · ' + elementTypeLabel(r.type);
      label.appendChild(cb);
      label.appendChild(span);
      container.appendChild(label);
    });
    if (!bases.length) {
      if (basesAll.length && ex) {
        container.innerHTML =
          '<div class="list-group-item text-muted small border-0">Aucun autre élément de base à rattacher (le parent cliqué dans l’arbre est exclu).</div>';
      } else {
        container.innerHTML =
          '<div class="list-group-item text-muted small border-0">Aucun élément de base (numérique ou chaîne). Créez-en dans le panneau précédent.</div>';
      }
    }
  }

  function getConsolidateSelectedIds() {
    var container = $('consolidate-pick-list');
    if (!container) return [];
    var ids = [];
    container.querySelectorAll('input.consolidate-pick:checked').forEach(function (cb) {
      ids.push(cb.value);
    });
    return ids;
  }

  function refreshElements() {
    if (!state.client || !state.database || !state.dimension) return Promise.resolve();
    return state.client
      .listElements(state.database.database, state.dimension.dimension)
      .then(function (rows) {
        state.lastElementRows = rows;
        var tree = $('element-tree');
        tree.innerHTML = '';
        var built = buildElementTree(rows);
        var ul = el('ul', 'list-group list-group-flush border rounded element-tree-list');
        renderTree(ul, built.byId, built.roots);
        tree.appendChild(ul);
        refreshConsolidationPickerFromRows(rows);
      });
  }

  function refreshRules() {
    if (!state.client || !state.database || !state.cube) return Promise.resolve();
    return state.client.listRules(state.database.database, state.cube.cube).then(function (rules) {
      var list = $('rule-list');
      list.innerHTML = '';
      rules.forEach(function (r) {
        var row = el('div', 'd-flex gap-2 align-items-start mb-2');
        var pre = el('pre');
        pre.className = 'list-row-main flex-grow-1 p-2 mb-0 bg-light border rounded small';
        pre.style.whiteSpace = 'pre-wrap';
        pre.textContent = r.rule_string || '';
        row.appendChild(pre);
        if (isNormalCubeRow(state.cube)) {
          var delRule = el('button', 'btn btn-sm btn-outline-danger rounded-circle list-row-delete flex-shrink-0');
          delRule.type = 'button';
          delRule.setAttribute('aria-label', 'Supprimer la règle');
          delRule.title = 'Supprimer';
          delRule.textContent = '−';
          delRule.addEventListener('click', function () {
            deleteRuleRow(r);
          });
          row.appendChild(delRule);
        }
        list.appendChild(row);
      });
    });
  }

  function syncAdminListToggleUI() {
    var wrap = $('nav-admin-list-mode-wrap');
    var chk = $('chk-admin-full-lists');
    if (wrap) wrap.classList.toggle('d-none', !state.isAdmin);
    if (chk) chk.checked = !!state.adminFullListView;
  }

  function bindAdminListMode() {
    var chk = $('chk-admin-full-lists');
    if (!chk) return;
    chk.addEventListener('change', function () {
      state.adminFullListView = !!chk.checked;
      state.dimension = null;
      state.cube = null;
      state.detailMode = null;
      state.lastElementRows = null;
      state.excludeFromConsolidationPick = null;
      state.consolidationAnchorElement = null;
      var colD = $('col-detail');
      if (colD) colD.classList.add('d-none');
      var colC = $('col-consolidate');
      if (colC) colC.classList.add('d-none');
      updateToolbarButtons();
      if (state.database) {
        refreshDimensionsAndCubes();
      }
      refreshDatabases();
    });
  }

  function bindLogin() {
    $('btn-login').addEventListener('click', function () {
      clearMessages();
      var base = $('inp-base').value.trim() || 'http://127.0.0.1:7777';
      var user = $('inp-user').value.trim();
      var pass = $('inp-pass').value;
      if (!user) {
        showError('Identifiant requis.');
        return;
      }
      state.baseUrl = base;
      state.client = new PaloClient(base);
      $('btn-login').disabled = true;
      state.client
        .login(user, pass)
        .then(function () {
          state.username = user;
          var navOut = $('nav-auth-out');
          var navIn = $('nav-auth-in');
          var navUser = $('nav-user-display');
          if (navOut) navOut.classList.add('d-none');
          if (navIn) navIn.classList.remove('d-none');
          if (navUser) navUser.textContent = 'Connecté : ' + user;
          var hint = $('app-login-hint');
          if (hint) hint.classList.add('d-none');
          $('col-databases').classList.remove('d-none');
          $('inp-pass').value = '';
          hideModal('modalLogin');
          return state.client.userInfo().catch(function () {
            return { isAdmin: false };
          });
        })
        .then(function (info) {
          state.isAdmin = !!(info && info.isAdmin);
          state.adminFullListView = false;
          var badge = $('logged-admin-badge');
          if (badge) badge.classList.toggle('d-none', !state.isAdmin);
          var btnUsers = $('btn-open-users-modal');
          if (btnUsers) btnUsers.classList.toggle('d-none', !state.isAdmin);
          syncAdminListToggleUI();
          showOk('Connecté.');
          return refreshDatabases();
        })
        .catch(function (e) {
          showError(e.message || String(e));
          state.client = null;
        })
        .then(function () {
          $('btn-login').disabled = false;
        });
    });

    $('btn-logout').addEventListener('click', function () {
      if (state.client) {
        state.client.logout().catch(function () {});
      }
      state.client = null;
      state.database = null;
      state.dimension = null;
      state.cube = null;
      state.detailMode = null;
      state.lastElementRows = null;
      state.excludeFromConsolidationPick = null;
      state.consolidationAnchorElement = null;
      state.isAdmin = false;
      state.adminFullListView = false;
      syncAdminListToggleUI();
      clearServerUsersCtx();
      var badge = $('logged-admin-badge');
      if (badge) badge.classList.add('d-none');
      var btnUsers = $('btn-open-users-modal');
      if (btnUsers) btnUsers.classList.add('d-none');
      var navOut = $('nav-auth-out');
      var navIn = $('nav-auth-in');
      var navUser = $('nav-user-display');
      if (navOut) navOut.classList.remove('d-none');
      if (navIn) navIn.classList.add('d-none');
      if (navUser) navUser.textContent = '';
      var hint = $('app-login-hint');
      if (hint) hint.classList.remove('d-none');
      $('col-databases').classList.add('d-none');
      $('col-db-content').classList.add('d-none');
      $('col-detail').classList.add('d-none');
      var colC = $('col-consolidate');
      if (colC) colC.classList.add('d-none');
      var sysHint = $('db-content-system-hint');
      if (sysHint) sysHint.classList.add('d-none');
      updateToolbarButtons();
      clearMessages();
    });
  }

  function bindDb() {
    $('btn-db-create').addEventListener('click', function () {
      clearMessages();
      var name = $('inp-new-db').value.trim();
      if (!name || !state.client) return;
      state.client
        .createDatabase(name)
        .then(function () {
          $('inp-new-db').value = '';
          hideModal('modalNewDatabase');
          showOk('Base créée.');
          return refreshDatabases();
        })
        .catch(function (e) {
          showError(e.message || String(e));
        });
    });
  }

  function bindDimCube() {
    $('btn-dim-create').addEventListener('click', function () {
      clearMessages();
      if (!state.client || !state.database) return;
      var name = $('inp-new-dim').value.trim();
      if (!name) return;
      state.client
        .createDimension(state.database.database, name)
        .then(function () {
          $('inp-new-dim').value = '';
          hideModal('modalNewDimension');
          showOk('Dimension créée.');
          return refreshDimensionsAndCubes();
        })
        .catch(function (e) {
          showError(e.message || String(e));
        });
    });

    $('btn-cube-create').addEventListener('click', function () {
      clearMessages();
      if (!state.client || !state.database) return;
      var name = $('inp-new-cube').value.trim();
      var dimIds = getSelectedCubeDimensionIds();
      if (!name) {
        showError('Indiquez un nom de cube.');
        return;
      }
      if (!dimIds.length) {
        showError('Sélectionnez au moins une dimension (clic sur la ligne).');
        return;
      }
      state.client
        .createCube(state.database.database, name, dimIds.join(','))
        .then(function () {
          $('inp-new-cube').value = '';
          hideModal('modalNewCube');
          var picker = $('cube-dim-picker');
          if (picker) picker.innerHTML = '';
          showOk('Cube créé.');
          return refreshDimensionsAndCubes();
        })
        .catch(function (e) {
          showError(e.message || String(e));
        });
    });
  }

  function wireCubeModal() {
    var modal = $('modalNewCube');
    if (!modal) return;
    modal.addEventListener('show.bs.modal', function () {
      populateCubeDimensionPicker();
    });
  }

  function bindElements() {
    $('btn-element-create').addEventListener('click', function () {
      clearMessages();
      if (!state.client || !state.database || !state.dimension || !isDimensionElementsMutable(state.dimension)) return;
      var name = $('inp-el-name').value.trim();
      var type = parseInt($('inp-el-type').value, 10);
      if (!name) return;
      if (type !== 1 && type !== 2) {
        showError('Type non pris en charge ici (utilisez le panneau Consolidation).');
        return;
      }
      state.client
        .createElement(state.database.database, state.dimension.dimension, name, type, undefined, undefined)
        .then(function () {
          $('inp-el-name').value = '';
          $('inp-el-type').value = '1';
          hideModal('modalNewElement');
          showOk('Élément créé.');
          return refreshElements();
        })
        .catch(function (e) {
          showError(e.message || String(e));
        });
    });
  }

  function bindAttributes() {
    var btn = $('btn-attribute-create');
    if (!btn) return;
    btn.addEventListener('click', function () {
      clearMessages();
      if (
        !state.client ||
        !state.database ||
        !state.attributeDimension ||
        !isDimensionElementsMutable(state.attributeDimension)
      ) {
        return;
      }
      var inp = $('inp-new-attr-name');
      var name = inp && inp.value ? inp.value.trim() : '';
      if (!name) {
        showError('Indiquez un nom d’attribut.');
        return;
      }
      state.client
        .createElement(state.database.database, state.attributeDimension.dimension, name, 2, undefined, undefined)
        .then(function () {
          if (inp) inp.value = '';
          hideModal('modalNewAttribute');
          showOk('Attribut créé.');
          return refreshAttributePanel();
        })
        .catch(function (e) {
          showError(e.message || String(e));
        });
    });
  }

  function bindConsolidate() {
    $('btn-consolidate-create').addEventListener('click', function () {
      clearMessages();
      if (!state.client || !state.database || !state.dimension || !isNormalDimensionRow(state.dimension)) return;
      var anchor = state.consolidationAnchorElement;
      if (!anchor || anchor.element == null) {
        showError('Cliquez sur l’élément parent dans l’arbre (nœud qui deviendra consolidé).');
        return;
      }
      var parentId = String(anchor.element);
      var childIds = getConsolidateSelectedIds();
      var weights = $('inp-consolidate-weights').value.trim().replace(/\s+/g, '');
      if (!childIds.length) {
        showError('Cochez au moins un élément de base à rattacher comme enfant.');
        return;
      }
      if (weights) {
        var parts = weights.split(',').filter(function (x) {
          return x !== '';
        });
        if (parts.length !== childIds.length) {
          showError('Le nombre de poids doit correspondre au nombre d’éléments cochés.');
          return;
        }
      }
      elementAppendRequest(
        state.client,
        state.database.database,
        state.dimension.dimension,
        parentId,
        childIds.join(','),
        weights || undefined
      )
        .then(function () {
          $('inp-consolidate-weights').value = '';
          var list = $('consolidate-pick-list');
          if (list) {
            list.querySelectorAll('input.consolidate-pick').forEach(function (cb) {
              cb.checked = false;
            });
          }
          showOk('Consolidation appliquée : enfants ajoutés sous l’élément (type consolidé si nécessaire).');
          return refreshElements();
        })
        .catch(function (e) {
          showError(e.message || String(e));
        });
    });
  }

  function bindRules() {
    $('btn-rule-create').addEventListener('click', function () {
      clearMessages();
      if (!state.client || !state.database || !state.cube || !isNormalCubeRow(state.cube)) return;
      var def = $('inp-rule-def').value;
      if (!def.trim()) return;
      state.client
        .createRule(state.database.database, state.cube.cube, def)
        .then(function () {
          $('inp-rule-def').value = '';
          hideModal('modalNewRule');
          showOk('Règle créée.');
          return refreshRules();
        })
        .catch(function (e) {
          showError(e.message || String(e));
        });
    });
  }

  function wireModalFocus() {
    [
      'modalUsers',
      'modalLogin',
      'modalNewDatabase',
      'modalNewDimension',
      'modalNewCube',
      'modalNewElement',
      'modalNewAttribute',
      'modalNewRule',
    ].forEach(function (mid) {
      var modal = $(mid);
      if (!modal) return;
      modal.addEventListener('shown.bs.modal', function () {
        var focusable = modal.querySelector('input, textarea, select');
        if (focusable) focusable.focus();
      });
    });
  }

  function init() {
    $('inp-base').value = state.baseUrl;
    updateToolbarButtons();
    wireModalFocus();
    wireCubeModal();
    bindLogin();
    bindAdminListMode();
    bindServerUsers();
    bindDb();
    bindDimCube();
    bindElements();
    bindAttributes();
    bindConsolidate();
    bindRules();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
