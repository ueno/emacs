;;; wl-win.el --- lisp side of interface with Wayland -*- lexical-binding: t -*-

;; Copyright (C) 1993-1994, 2005-2014 Free Software Foundation, Inc.

;; Author: FSF
;; Keywords: terminals, i18n

;; This file is part of GNU Emacs.

;; GNU Emacs is free software: you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;; GNU Emacs is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs.  If not, see <http://www.gnu.org/licenses/>.

;;; Code:
(eval-when-compile (require 'cl-lib))
(or (featurep 'wl)
    (error "%s: Loading wl-win.el but not compiled for Wayland"
           (invocation-name)))

;;;; Command line argument handling.

;; Set in term/common-win.el; currently unused by Nextstep's x-open-connection.
(defvar x-command-line-resources)

(defvar wl-initialized nil
  "Non-nil if Wayland windowing has been initialized.")

(if (fboundp 'new-fontset)
    (require 'fontset))

(declare-function x-handle-args "common-win" (args))
(declare-function x-open-connection "wlfns.m"
                  (display &optional xrm-string must-succeed))

;; Do the actual Wayland setup here; the above code just defines
;; functions and variables that we use now.
(defun wl-initialize-window-system (&optional display)
  "Initialize Emacs for Wayland windowing."
  (cl-assert (not wl-initialized))

  ;; Setup the default fontset.
  (create-default-fontset)

  ;; Create the standard fontset.
  (condition-case err
	(create-fontset-from-fontset-spec standard-fontset-spec t)
    (error (display-warning
	    'initialization
	    (format "Creation of the standard fontset failed: %s" err)
	    :error)))

  (x-open-connection (or display
			 (or (getenv "WAYLAND_DISPLAY" (selected-frame))
			     (getenv "WAYLAND_DISPLAY")))
		     x-command-line-resources t)

  (setq wl-initialized t))

;; Any display name is OK.
(add-to-list 'display-format-alist '(".*" . wl))
(add-to-list 'handle-args-function-alist '(wl . x-handle-args))
(add-to-list 'frame-creation-function-alist '(wl . x-create-frame-with-faces))
(add-to-list 'window-system-initialization-alist '(wl . wl-initialize-window-system))


(provide 'wl-win)

;;; wl-win.el ends here
