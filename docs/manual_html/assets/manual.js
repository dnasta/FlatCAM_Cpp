/* FlatCAM C++ — Manual de usuario */
(function () {
  const root = document.documentElement;
  const body = document.body;

  // Tema: preferencia guardada > preferencia del sistema.
  const saved = localStorage.getItem("fc-manual-theme");
  if (saved === "dark" || saved === "light") {
    root.dataset.theme = saved;
  } else if (window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches) {
    root.dataset.theme = "dark";
  }

  const themeBtn = document.querySelector("[data-theme-toggle]");
  if (themeBtn) {
    const label = () => {
      themeBtn.textContent = root.dataset.theme === "dark" ? "☀" : "☾";
      themeBtn.title = root.dataset.theme === "dark"
        ? "Cambiar a tema claro" : "Cambiar a tema oscuro";
    };
    label();
    themeBtn.addEventListener("click", () => {
      root.dataset.theme = root.dataset.theme === "dark" ? "light" : "dark";
      localStorage.setItem("fc-manual-theme", root.dataset.theme);
      label();
    });
  }

  // Navegación móvil.
  const menuBtn = document.querySelector("[data-menu-toggle]");
  if (menuBtn) {
    menuBtn.addEventListener("click", () => body.classList.toggle("nav-open"));
    document.addEventListener("click", (event) => {
      if (window.innerWidth > 960) return;
      if (!body.classList.contains("nav-open")) return;
      if (event.target.closest(".sidebar") || event.target.closest("[data-menu-toggle]")) return;
      body.classList.remove("nav-open");
    });
  }

  // Marca el enlace activo.
  const current = location.pathname.split("/").pop() || "index.html";
  document.querySelectorAll(".sidebar nav a").forEach((link) => {
    const href = link.getAttribute("href");
    if (href === current) link.classList.add("active");
  });

  // Búsqueda en la navegación.
  const search = document.querySelector("[data-nav-search]");
  if (search) {
    search.addEventListener("input", () => {
      const q = search.value.trim().toLowerCase();
      document.querySelectorAll(".sidebar nav a").forEach((link) => {
        const text = (link.textContent || "").toLowerCase();
        link.classList.toggle("hidden", q.length > 0 && !text.includes(q));
      });
      document.querySelectorAll(".nav-group").forEach((group) => {
        const visible = group.querySelectorAll("a:not(.hidden)").length > 0;
        group.style.display = visible ? "" : "none";
      });
    });
    document.addEventListener("keydown", (event) => {
      if (event.key === "/" && !/input|textarea|select/i.test(document.activeElement.tagName)) {
        event.preventDefault();
        search.focus();
      }
      if (event.key === "Escape" && document.activeElement === search) {
        search.value = "";
        search.dispatchEvent(new Event("input"));
        search.blur();
      }
    });
  }

  // Botón volver arriba.
  const toTop = document.querySelector("[data-to-top]");
  if (toTop) {
    const update = () => toTop.classList.toggle("show", window.scrollY > 600);
    window.addEventListener("scroll", update, { passive: true });
    update();
    toTop.addEventListener("click", () => window.scrollTo({ top: 0, behavior: "smooth" }));
  }

  // Copiar bloques de código.
  document.querySelectorAll("pre").forEach((pre) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "copy-btn";
    button.textContent = "Copiar";
    button.addEventListener("click", async () => {
      const text = pre.querySelector("code")?.innerText || pre.innerText;
      try {
        await navigator.clipboard.writeText(text);
        button.textContent = "¡Copiado!";
        setTimeout(() => { button.textContent = "Copiar"; }, 1400);
      } catch (_) {
        button.textContent = "Error";
      }
    });
    pre.appendChild(button);
  });
})();
