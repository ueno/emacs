;; enwc-setup.el - Setup routines for ENWC

;; Copyright (C) 2012,2013 Free Software Foundation

;; Author: Ian Dunn
;; Keywords: enwc, network, wicd, manager, nm

;; This file is part of ENWC

;; ENWC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.

;; ENWC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with ENWC; see the file COPYING.  If not, write to the Free
;; Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
;; 02110-1301, USA.


(require 'enwc)
(require 'enwc-wicd)
(require 'enwc-nm)

(defun enwc-setup-backend (cur-back)
  "Sets up ENWC to use the correct function for the backend CUR-BACK."
  (let ((sym-name (symbol-name cur-back)))
    (setq enwc-scan-func (intern (concat "enwc-"
					 sym-name
					 "-scan"))
	  enwc-get-nw-func (intern (concat "enwc-"
					   sym-name
					   "-get-networks"))
	  enwc-get-wireless-nw-prop-func (intern (concat "enwc-"
							 sym-name
							 "-get-wireless-network-property"))
	  enwc-get-wireless-nw-props-func (intern (concat "enwc-"
							  sym-name
							  "-get-wireless-nw-props"))
	  enwc-details-list (symbol-value (intern (concat "enwc-"
							  sym-name
							  "-details-list")))
	  enwc-get-encryption-type-func (intern (concat "enwc-"
							sym-name
							"-get-encryption-type"))
	  enwc-wireless-connect-func (intern (concat "enwc-"
						     sym-name
						     "-connect"))
	  enwc-get-current-nw-id-func (intern (concat "enwc-"
						      sym-name
						      "-get-current-nw-id"))
	  enwc-check-connecting-func (intern (concat "enwc-"
						     sym-name
						     "-check-connecting"))
	  enwc-get-wired-profiles-func (intern (concat "enwc-"
						       sym-name
						       "-get-wired-profiles"))
	  enwc-is-wired-func (intern (concat "enwc-"
					     sym-name
					     "-is-wired"))
	  enwc-wired-connect-func (intern (concat "enwc-"
						  sym-name
						  "-wired-connect"))
	  enwc-wired-disconnect-func (intern (concat "enwc-"
						     sym-name
						     "-wired-disconnect"))
	  enwc-get-sec-types-func (intern (concat "enwc-"
						  sym-name
						  "-get-sec-types"))
	  enwc-get-ip-addr-func (intern (concat "enwc-"
						sym-name
						"-get-ip-addr"))
	  enwc-get-netmask-func (intern (concat "enwc-"
						sym-name
						"-get-netmask"))
	  enwc-get-gateway-func (intern (concat "enwc-"
						sym-name
						"-get-gateway"))
	  enwc-get-dns-func (intern (concat "enwc-"
					    sym-name
					    "-get-dns"))
	  enwc-get-nw-info-func (intern (concat "enwc-"
						sym-name
						"-get-nw-info"))
	  enwc-save-nw-settings-func (intern (concat "enwc-"
						     sym-name
						     "-save-nw-settings"))
	  )
    (funcall (intern (concat "enwc-" sym-name "-setup")))))

(defun enwc-setup ()
  "Sets up ENWC.
This setups ENWC and confirms that one of the backends can be found
on D-Bus."
  (setq global-mode-string (append global-mode-string
				   '(enwc-display-string)))
  (run-at-time t 1 'enwc-update-mode-line)

  (let ((cur-back nil)
	(back-list enwc-backends))
    (while (and back-list (not cur-back))
      (setq cur-back (pop back-list))
      (if (not (dbus-ping :system
			  (symbol-value (intern (concat "enwc-"
							(symbol-name cur-back)
							"-dbus-service")))))
	  (setq cur-back nil)))
    (if cur-back
	(enwc-setup-backend cur-back)
      (error "No usable backend found."))))

(provide 'enwc-setup)

;;; End of File.
