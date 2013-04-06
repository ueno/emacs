;;; enwc-wicd.el --- The Wicd backend to ENWC

;; Copyright (C) 2012,2013 Free Software Foundation, Inc.

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


;;; Commentary:
;; TODO

(require 'enwc)

(defgroup enwc-wicd nil
  "*Wicd variables for ENWC."
  :prefix "enwc-wicd-"
  :group 'enwc)

(defcustom enwc-wicd-dbus-service "org.wicd.daemon"
  "The wicd D-Bus service identifier."
  :group 'enwc-wicd
  :type 'string)

(defcustom enwc-wicd-dbus-wireless-path "/org/wicd/daemon/wireless"
  "The wicd wireless D-Bus path."
  :group 'enwc-wicd
  :type 'string)

(defcustom enwc-wicd-dbus-wireless-interface "org.wicd.daemon.wireless"
  "The wicd wireless D-Bus interface."
  :group 'enwc-wicd
  :type 'string)

(defcustom enwc-wicd-dbus-wired-path "/org/wicd/daemon/wired"
  "The wicd wired D-Bus path."
  :group 'enwc-wicd
  :type 'string)

(defcustom enwc-wicd-dbus-wired-interface "org.wicd.daemon.wired"
  "The wicd wired D-Bus interface."
  :group 'enwc-wicd
  :type 'string)

(defvar enwc-wicd-details-list
  '("essid" "bssid" "quality" "encryption" "mode" "channel")
  "The list of the desired details to be obtained from each network.")

(defvar enwc-wicd-current-ap "")

(defvar enwc-wicd-current-nw-id -1)

(defun enwc-wicd-dbus-wireless-call-method (method &rest args)
  "Calls D-Bus method METHOD with arguments ARGS within
the wicd wireless interface."
  (apply 'dbus-call-method :system
	 enwc-wicd-dbus-service
	 enwc-wicd-dbus-wireless-path
	 enwc-wicd-dbus-wireless-interface
	 method
	 :timeout 25000
	 args))

(defun enwc-wicd-dbus-wired-call-method (method &rest args)
  "Calls D-Bus method METHOD with arguments ARGS within
the wicd wired interface."
  (apply 'dbus-call-method :system
	 enwc-wicd-dbus-service
	 enwc-wicd-dbus-wired-path
	 enwc-wicd-dbus-wired-interface
	 method
	 :timeout 25000
	 args))

(defun enwc-wicd-scan ()
  "Wicd scan function."
  (enwc-wicd-dbus-wireless-call-method "Scan"))

(defun enwc-wicd-get-networks ()
  "Wicd get networks function.  Just returns a number sequence."
  (number-sequence 0 (1- (enwc-wicd-dbus-wireless-call-method "GetNumberOfNetworks"))))

(defvar enwc-wicd-prop-values nil)
(defvar enwc-wicd-prop-num 0)

(defun enwc-wicd-nw-prop-handler (&rest args)
  (setq enwc-wicd-prop-values (cons args enwc-wicd-prop-values))
  (setq enwc-wicd-prop-num (1+ enwc-wicd-prop-num)))

(defun enwc-wicd-get-wireless-network-property (id prop)
  "Wicd get wireless network property function.
This calls the D-Bus method on Wicd to get the property PROP
from wireless network with id ID."
  (dbus-call-method-asynchronously :system
				   enwc-wicd-dbus-service
				   enwc-wicd-dbus-wireless-path
				   enwc-wicd-dbus-wireless-interface
				   "GetWirelessProperty"
				   'enwc-wicd-nw-prop-handler
				   :int32 id
				   :string prop)

  ;;(enwc-wicd-dbus-wireless-call-method "GetWirelessProperty" id prop)
  )

(defun enwc-wicd-build-prop-list (prop-list det-list)
  (let (ret
	(act-det-list (reverse det-list)))
    (while prop-list
      (let ((cur-prop (pop prop-list))
	    (cur-det (pop act-det-list)))
	(setq ret (append ret (cons (cons cur-det (car cur-prop)) nil)))
	))
    ret))

(defun enwc-wicd-get-wireless-nw-props (id)
  (setq enwc-wicd-prop-values nil)
  (setq enwc-wicd-prop-num 0)
  (mapc (lambda (x)
	    (enwc-wicd-get-wireless-network-property id x))
	  enwc-wicd-details-list)
  ;; Wait for less than a second.
  (while (< enwc-wicd-prop-num 6)
    (read-event nil nil 0.001))
  (enwc-wicd-build-prop-list enwc-wicd-prop-values enwc-wicd-details-list))

(defun enwc-wicd-get-encryption-type (id)
  "Wicd get encryption type function.
This calls the D-Bus method on Wicd to get the encryption_method
property from wireless network with id ID."
  (enwc-wicd-dbus-wireless-call-method "GetWirelessProperty"
				       id "encryption_method"))

(defun enwc-wicd-connect (id)
  "Wicd connect function.
This calls the D-Bus method on Wicd to connect to wireless
network with id ID."
  (enwc-wicd-dbus-wireless-call-method "ConnectWireless" id))

(defun enwc-wicd-get-current-nw-id (wired)
  "Wicd get current network id function.
This calls the D-Bus method on Wicd to get the current
wireless network id."
  ;;(enwc-wicd-dbus-wireless-call-method "GetCurrentNetworkID"))
  (if wired
      -1
    enwc-wicd-current-nw-id))

(defun enwc-wicd-check-connecting ()
  "The Wicd check connecting function."
  (enwc-wicd-dbus-wireless-call-method "CheckIfWirelessConnecting"))

(defun enwc-wicd-disconnect ()
  "Wicd disconnect function."
  (enwc-wicd-dbus-wireless-call-method "DisconnectWireless"))

(defun enwc-wicd-get-wired-profiles ()
  "Gets the list of wired network profiles."
  (enwc-wicd-dbus-wired-call-method "GetWiredProfileList"))

(defun enwc-wicd-wired-connect (id)
  "Connects to the wired network with profile id ID."
  (let* ((profs (enwc-get-wired-profiles))
	 (prf (nth id profs)))
    (enwc-wicd-dbus-wired-call-method "ReadWiredNetworkProfile" prf)
    (enwc-wicd-dbus-wired-call-method "ConnectWired")))

(defun enwc-wicd-wired-disconnect ()
  "Disconnects from the wired connection."
  (enwc-wicd-dbus-wired-call-method "DisconnectWired"))

(defun enwc-wicd-is-wired ()
  "Checks to see if wired is connected."
  (not (not (enwc-wicd-dbus-wired-call-method "GetWiredIP"))))

(defun enwc-wicd-get-wired-nw-prop (id det)
  "Gets property DET from the wired network with id ID."
  (enwc-wicd-dbus-wired-call-method "GetWiredProperty" id det))

;; Each entry in sec-types should be:
;; ("IDENT" (("Name" . "NAME") ("reqs" . (("key1" . "Entry1") ("key2" . "Entry2") ... ))))
;; Where:
;;  "IDENT" => String that identifies this to the backend.
;;  "NAME" => String that ENWC displays
;;  "reqs" => Constant string, but the association list holds entries
;;              required by the security type, i.e. user, passphrase, etc.
;;  "keyXX" => String that the backend uses for this security entry.
;;  "EntryXX" => String that ENWC displays for this security entry.

(defun enwc-wicd-get-sec-types (wired)
  "Gets the list of security types.
WIRED indicates whether this is a wired connection.
The returned list will be in the format:
 (name . ((\"Name\" . \"DISPLAY-NAME\")
          (\"reqs\" . ((\"Display\" . \"id\") ...))))"
  (let (sec-types
	ret-list)
    (with-temp-buffer
      (insert-file-contents (concat "/etc/wicd/encryption/templates/active"
				    (if wired
					"_wired")))
      (setq sec-types (split-string (buffer-string) "\n")))
    (setq ret-list
	  (mapcar (lambda (x)
		    (if (not (eq (length x) 0))
			(let (name reqs)
			  (with-temp-buffer
			    (insert-file-contents (concat "/etc/wicd/encryption/templates/"
							  x))
			    (re-search-forward "name[ \t]*=[ \t]*\\([^\n]*\\)[\n]")
			    (setq name (match-string 1))
			    (re-search-forward "require[ \t]*\\([^\n]*\\)[\n]")
			    (let ((str-reqs (split-string (match-string 1) " ")))
			      (while str-reqs
				(setq reqs
				      (append reqs
					      (cons (cons (pop str-reqs)
							  (pop str-reqs))
						    nil)))))
			    (cons x (cons (cons "Name" name) (cons (cons "reqs" (cons reqs nil)) nil)))
			    ))))
		  sec-types))))

(defun enwc-wicd-get-profile-ent (wired id ent)
  "Gets profile entry ENT from the network with id ID.
WIRED is set to indicate whether this is a wired network.
This function is a wrapper around the *-get-(wired|wireless)-nw-prop
functions, allowing for a single function that checks for wired."
  (if wired
      (enwc-wicd-get-wired-nw-prop id ent)
    (enwc-wicd-dbus-wireless-call-method "GetWirelessProperty" id ent)))

(defun enwc-wicd-get-nw-info (wired id)
  (let ((dns-list (enwc-wicd-get-dns wired id)))
    (list (cons (cons "addr" (enwc-wicd-get-ip-addr wired id)) nil)
	  (cons (cons "netmask" (enwc-wicd-get-netmask wired id)) nil)
	  (cons (cons "gateway" (enwc-wicd-get-gateway wired id)) nil)
	  (cons (cons "dns1" (nth 0 dns-list)) nil)
	  (cons (cons "dns2" (nth 1 dns-list)) nil))))

(defun enwc-wicd-get-ip-addr (wired id)
  "Gets the IP Address from the network with id ID.
Wired is set to indicate whether this is a wired network."
  (or (enwc-wicd-get-profile-ent wired id "ip") ""))

(defun enwc-wicd-get-netmask (wired id)
  "Gets the Netmask from the network with id ID.
WIRED is set to indicate whether this is a wired network."
  (or (enwc-wicd-get-profile-ent wired id "netmask") ""))

(defun enwc-wicd-get-gateway (wired id)
    "Gets the Gateway from the network with id ID.
WIRED is set to indicate whether this is a wired network."
  (or (enwc-wicd-get-profile-ent wired id "gateway") ""))

(defun enwc-wicd-get-dns (wired id)
    "Gets the list of DNS servers from the network with id ID.
WIRED is set to indicate whether this is a wired network."
  (list (or (enwc-wicd-get-profile-ent wired id "dns1") "")
	(or (enwc-wicd-get-profile-ent wired id "dns2") "")
	(or (enwc-wicd-get-profile-ent wired id "dns3") "")))

(defun enwc-wicd-set-nw-prop (wired id prop val)
  "Sets the network property PROP of the network with id ID
to VAL.
WIRED indicates whether this is a wired network."
  (if wired
      (enwc-wicd-dbus-wired-call-method "SetWiredProperty"
					id prop val)
    (enwc-wicd-dbus-wireless-call-method "SetWirelessProperty"
					 id prop val)))

(defun enwc-wicd-save-nw-profile (wired id)
  "Save the network profile with for the network with id ID.
WIRED indicates whether this is a wired network."
  (if wired
      (enwc-wicd-dbus-wired-call-method "SaveWiredNetworkProfile" id)
    (enwc-wicd-dbus-wireless-call-method "SaveWirelessNetworkProfile" id)))

(defun enwc-wicd-save-nw-settings (wired id settings)
  "Saves the settings indicated by the association list SETTINGS for
the network with id ID."
  (let ((enctype (cdr (assoc "enctype" settings))))

    (enwc-wicd-set-nw-prop wired id "ip"
			   (cdr (assoc "addr" settings)))
    (enwc-wicd-set-nw-prop wired id "netmask"
			   (cdr (assoc "netmask" settings)))
    (enwc-wicd-set-nw-prop wired id "gateway"
			   (cdr (assoc "gateway" settings)))

    (enwc-wicd-set-nw-prop wired id "dns1"
			   (cdr (assoc "dns1" settings)))
    (enwc-wicd-set-nw-prop wired id "dns2"
			   (cdr (assoc "dns2" settings)))

    (enwc-wicd-set-nw-prop wired id "enctype" enctype)
    (if (not (string= enctype "None"))
	(dolist (x (cadr (assoc "reqs"
				(cdr (assoc enctype
					    (enwc-wicd-get-sec-types wired))))))
	  (enwc-wicd-set-nw-prop wired id (car x)
				 (cdr (assoc (car x) settings)))))
    (enwc-wicd-save-nw-profile wired id))
  )

(defun enwc-wicd-wireless-prop-changed (state info)
  (if state
      (if (eq state 0)
	  (setq enwc-wicd-current-ap ""
		enwc-wicd-current-nw-id -1)
	(setq enwc-wicd-current-ap (car (cadr info))
	      enwc-wicd-current-nw-id (or (and info
					       (nthcdr 3 info)
					       (caar (nthcdr 3 info))
					       (string-to-number (caar (nthcdr 3 info))))
					  -1)))
  ))

(defun enwc-wicd-setup ()
  ;; Thanks to Michael Albinus for pointing out this signal.
  (dbus-register-signal :system
			enwc-wicd-dbus-service
			enwc-wicd-dbus-wireless-path
			enwc-wicd-dbus-wireless-interface
			"SendEndScanSignal"
			'enwc-process-scan)

  (dbus-register-signal :system
			enwc-wicd-dbus-service
			"/org/wicd/daemon"
			enwc-wicd-dbus-service
			"StatusChanged"
			'enwc-wicd-wireless-prop-changed)
  )

(provide 'enwc-wicd)

;;; End of File
