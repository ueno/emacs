;;; vlf.el --- View Large Files  -*- lexical-binding: t -*-

;; Copyright (C) 2006, 2012, 2013  Free Software Foundation, Inc.

;; Version: 0.6
;; Keywords: large files, utilities
;; Authors: 2006 Mathias Dahl <mathias.dahl@gmail.com>
;;          2012 Sam Steingold <sds@gnu.org>
;;          2013 Andrey Kotlarski <m00naticus@gmail.com>

;; This file is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; This file is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs; see the file COPYING.  If not, write to
;; the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
;; Boston, MA 02111-1307, USA.

;;; Commentary:

;; This package provides the M-x vlf command, which visits part of a
;; large file in a read-only buffer without visiting the entire file.
;; The buffer uses VLF mode, which defines several commands for
;; moving around, searching and editing selected chunk of file.

;; This package was inspired by a snippet posted by Kevin Rodgers,
;; showing how to use `insert-file-contents' to extract part of a
;; file.

;;; Code:

(defgroup vlf nil
  "View Large Files in Emacs."
  :prefix "vlf-"
  :group 'files)

(defcustom vlf-batch-size 1024
  "Defines how large each batch of file data is (in bytes)."
  :type 'integer
  :group 'vlf)

;;; Keep track of file position.
(defvar vlf-start-pos 0
  "Absolute position of the visible chunk start.")
(defvar vlf-end-pos vlf-batch-size
  "Absolute position of the visible chunk end.")
(defvar vlf-file-size 0 "Total size of presented file.")

(defvar vlf-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map [M-next] 'vlf-next-batch)
    (define-key map [M-prior] 'vlf-prev-batch)
    (define-key map "+" 'vlf-change-batch-size)
    (define-key map "-"
      (lambda () "Decrease vlf batch size by factor of 2."
        (interactive)
        (vlf-change-batch-size t)))
    (define-key map "s" 'vlf-re-search-forward)
    (define-key map "r" 'vlf-re-search-backward)
    (define-key map "[" 'vlf-beginning-of-file)
    (define-key map "]" 'vlf-end-of-file)
    (define-key map "e" 'vlf-edit-mode)
    (define-key map "j" 'vlf-jump-to-chunk)
    map)
  "Keymap for `vlf-mode'.")

(define-derived-mode vlf-mode special-mode "VLF"
  "Mode to browse large files in."
  (setq buffer-read-only t)
  (set-buffer-modified-p nil)
  (buffer-disable-undo)
  (add-hook 'write-contents-functions 'vlf-write)
  (setq revert-buffer-function 'vlf-revert)
  (make-local-variable 'vlf-batch-size)
  (put 'vlf-batch-size 'permanent-local t)
  (make-local-variable 'vlf-start-pos)
  (put 'vlf-start-pos 'permanent-local t)
  (make-local-variable 'vlf-end-pos)
  (put 'vlf-end-pos 'permanent-local t)
  (make-local-variable 'vlf-file-size)
  (put 'vlf-file-size 'permanent-local t))

;;;###autoload
(defun vlf (file &optional from-end)
  "View Large FILE.  With FROM-END prefix, view from the back.
Batches of the file data from FILE will be displayed in a read-only
buffer.  You can customize number of bytes displayed by customizing
`vlf-batch-size'."
  (interactive "fFile to open: \nP")
  (with-current-buffer (generate-new-buffer "*vlf*")
    (setq buffer-file-name file
          vlf-file-size (nth 7 (file-attributes file)))
    (vlf-insert-file from-end)
    (vlf-mode)
    (switch-to-buffer (current-buffer))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; integration with other packages

;;;###autoload
(defun dired-vlf (from-end)
  "In Dired, visit the file on this line in VLF mode.
With FROM-END prefix, view from the back."
  (interactive "P")
  (vlf (dired-get-file-for-visit) from-end))

;;;###autoload
(eval-after-load "dired"
  '(define-key dired-mode-map "V" 'dired-vlf))

;;;###autoload
(defun vlf-if-file-too-large (size op-type &optional filename)
  "If file SIZE larger than `large-file-warning-threshold', \
allow user to view file with `vlf', open it normally or abort.
OP-TYPE specifies the file operation being performed over FILENAME."
  (and large-file-warning-threshold size
       (> size large-file-warning-threshold)
       (let ((char nil))
         (while (not (memq (setq char
                                 (read-event
                                  (propertize
                                   (format
                                    "File %s is large (%s): \
%s normally (o), %s with vlf (v) or abort (a)"
                                    (if filename
                                        (file-name-nondirectory filename)
                                      "")
                                    (file-size-human-readable size)
                                    op-type op-type)
                                   'face 'minibuffer-prompt)))
                           '(?o ?O ?v ?V ?a ?A))))
         (cond ((memq char '(?o ?O)))
               ((memq char '(?v ?V))
                (vlf filename nil)
                (error ""))
               ((memq char '(?a ?A))
                (error "Aborted"))))))

;; hijack `abort-if-file-too-large'
;;;###autoload
(fset 'abort-if-file-too-large 'vlf-if-file-too-large)

;; non recent Emacs
(unless (fboundp 'file-size-human-readable)
  (defun file-size-human-readable (file-size)
    "Print FILE-SIZE in MB."
    (format "%.1fMB" (/ file-size 1024.0))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; utilities

(defun vlf-change-batch-size (decrease)
  "Change the buffer-local value of `vlf-batch-size'.
Normally, the value is doubled;
with the prefix argument DECREASE it is halved."
  (interactive "P")
  (or (assq 'vlf-batch-size (buffer-local-variables))
      (error "%s is not local in this buffer" 'vlf-batch-size))
  (setq vlf-batch-size (if decrease
                            (/ vlf-batch-size 2)
                          (* vlf-batch-size 2)))
  (vlf-move-to-batch vlf-start-pos))

(defun vlf-format-buffer-name ()
  "Return format for vlf buffer name."
  (format "%s(%s)[%d/%d](%d)"
          (file-name-nondirectory buffer-file-name)
          (file-size-human-readable vlf-file-size)
          (/ vlf-end-pos vlf-batch-size)
          (/ vlf-file-size vlf-batch-size)
          vlf-batch-size))

(defun vlf-update-buffer-name ()
  "Update the current buffer name."
  (rename-buffer (vlf-format-buffer-name) t))

(defun vlf-insert-file (&optional from-end)
  "Insert first chunk of current file contents in current buffer.
With FROM-END prefix, start from the back."
  (if from-end
      (setq vlf-start-pos (max 0 (- vlf-file-size vlf-batch-size))
            vlf-end-pos vlf-file-size)
    (setq vlf-start-pos 0
          vlf-end-pos (min vlf-batch-size vlf-file-size)))
  (vlf-move-to-chunk vlf-start-pos vlf-end-pos))

(defun vlf-beginning-of-file ()
  "Jump to beginning of file content."
  (interactive)
  (vlf-insert-file))

(defun vlf-end-of-file ()
  "Jump to end of file content."
  (interactive)
  (vlf-insert-file t))

(defun vlf-revert (&rest args)
  "Revert current chunk.  Ignore ARGS."
  (ignore args)
  (vlf-move-to-chunk vlf-start-pos vlf-end-pos))

(defun vlf-jump-to-chunk (n)
  "Go to to chunk N."
  (interactive "nGoto to chunk: ")
  (vlf-move-to-batch (* (1- n) vlf-batch-size)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; batch movement

(defun vlf-next-batch (append)
  "Display the next batch of file data.
When prefix argument is supplied and positive
 jump over APPEND number of batches.
When prefix argument is negative
 append next APPEND number of batches to the existing buffer."
  (interactive "p")
  (let ((end (+ vlf-end-pos (* vlf-batch-size
                                (abs append)))))
    (when (< vlf-file-size end)        ; re-check file size
      (setq vlf-file-size (nth 7 (file-attributes buffer-file-name)))
      (cond ((= vlf-end-pos vlf-file-size)
             (error "Already at EOF"))
            ((< vlf-file-size end)
             (setq end vlf-file-size))))
    (let ((inhibit-read-only t)
          (do-append (< append 0))
          (pos (point)))
      (if do-append
          (goto-char (point-max))
        (setq vlf-start-pos (- end vlf-batch-size))
        (erase-buffer))
      (insert-file-contents buffer-file-name nil (if do-append
                                                     vlf-end-pos
                                                   vlf-start-pos)
                            end)
      (goto-char pos))
    (setq vlf-end-pos end))
  (set-visited-file-modtime)
  (set-buffer-modified-p nil)
  (vlf-update-buffer-name))

(defun vlf-prev-batch (prepend)
  "Display the previous batch of file data.
When prefix argument is supplied and positive
 jump over PREPEND number of batches.
When prefix argument is negative
 append previous PREPEND number of batches to the existing buffer."
  (interactive "p")
  (if (zerop vlf-start-pos)
      (error "Already at BOF"))
  (let ((inhibit-read-only t)
        (start (max 0 (- vlf-start-pos (* vlf-batch-size
                                           (abs prepend)))))
        (do-prepend (< prepend 0))
        (pos (- (point-max) (point))))
    (if do-prepend
        (goto-char (point-min))
      (setq vlf-end-pos (+ start vlf-batch-size))
      (erase-buffer))
    (insert-file-contents buffer-file-name nil start
                          (if do-prepend
                              vlf-start-pos
                            vlf-end-pos))
    (goto-char (- (point-max) pos))
    (setq vlf-start-pos start))
  (set-visited-file-modtime)
  (set-buffer-modified-p nil)
  (vlf-update-buffer-name))

(defun vlf-move-to-batch (start)
  "Move to batch determined by START.
Adjust according to file start/end and show `vlf-batch-size' bytes."
  (setq vlf-start-pos (max 0 start)
        vlf-end-pos (+ vlf-start-pos vlf-batch-size))
  (if (< vlf-file-size vlf-end-pos)   ; re-check file size
      (setq vlf-file-size
            (nth 7 (file-attributes buffer-file-name))
            vlf-end-pos (min vlf-end-pos vlf-file-size)
            vlf-start-pos (max 0 (- vlf-end-pos vlf-batch-size))))
  (let ((inhibit-read-only t)
        (pos (point)))
    (erase-buffer)
    (insert-file-contents buffer-file-name nil
                          vlf-start-pos vlf-end-pos)
    (goto-char pos))
  (set-visited-file-modtime)
  (set-buffer-modified-p nil)
  (vlf-update-buffer-name))

(defun vlf-move-to-chunk (start end)
  "Move to chunk determined by START END."
  (if (< vlf-file-size end)            ; re-check file size
      (setq vlf-file-size (nth 7
                                (file-attributes buffer-file-name))))
  (setq vlf-start-pos (max 0 start)
        vlf-end-pos (min end vlf-file-size))
  (let ((inhibit-read-only t)
        (pos (point)))
    (erase-buffer)
    (insert-file-contents buffer-file-name nil
                          vlf-start-pos vlf-end-pos)
    (goto-char pos))
  (set-visited-file-modtime)
  (set-buffer-modified-p nil)
  (vlf-update-buffer-name))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; search

(defun vlf-re-search (regexp count backward)
  "Search for REGEXP COUNT number of times forward or BACKWARD."
  (let* ((match-start-pos (+ vlf-start-pos (point)))
         (match-end-pos match-start-pos)
         (to-find count)
         (search-reporter (make-progress-reporter
                           (concat "Searching for " regexp)
                           (if backward
                               (- vlf-file-size vlf-end-pos)
                             vlf-start-pos)
                           vlf-file-size))
         (batch-step (/ vlf-batch-size 8))) ; amount of chunk overlap
    (unwind-protect
        (catch 'end-of-file
          (if backward
              (while (not (zerop to-find))
                (cond ((re-search-backward regexp nil t)
                       (setq to-find (1- to-find)
                             match-start-pos (+ vlf-start-pos
                                                (match-beginning 0))
                             match-end-pos (+ vlf-start-pos
                                              (match-end 0))))
                      ((zerop vlf-start-pos)
                       (throw 'end-of-file nil))
                      (t (let ((batch-move (- vlf-start-pos
                                              (- vlf-batch-size
                                                 batch-step))))
                           (vlf-move-to-batch
                            (if (< match-start-pos batch-move)
                                (- match-start-pos vlf-batch-size)
                              batch-move)))
                         (goto-char (if (< match-start-pos
                                           vlf-end-pos)
                                        (- match-start-pos
                                           vlf-start-pos)
                                      (point-max)))
                         (progress-reporter-update search-reporter
                                                   vlf-start-pos))))
            (while (not (zerop to-find))
              (cond ((re-search-forward regexp nil t)
                     (setq to-find (1- to-find)
                           match-start-pos (+ vlf-start-pos
                                              (match-beginning 0))
                           match-end-pos (+ vlf-start-pos
                                            (match-end 0))))
                    ((= vlf-end-pos vlf-file-size)
                     (throw 'end-of-file nil))
                    (t (let ((batch-move (- vlf-end-pos batch-step)))
                         (vlf-move-to-batch
                          (if (< batch-move match-end-pos)
                              match-end-pos
                            batch-move)))
                       (goto-char (if (< vlf-start-pos match-end-pos)
                                      (- match-end-pos vlf-start-pos)
                                    (point-min)))
                       (progress-reporter-update search-reporter
                                                 vlf-end-pos)))))
          (progress-reporter-done search-reporter))
      (if backward
          (vlf-goto-match match-end-pos match-start-pos
                           count to-find)
        (vlf-goto-match match-start-pos match-end-pos
                         count to-find)))))

(defun vlf-goto-match (match-pos-start match-pos-end count to-find)
  "Move to chunk surrounding MATCH-POS-START and MATCH-POS-END.
According to COUNT and left TO-FIND, show if search has been
successful.  Return nil if nothing found."
  (let ((success (zerop to-find)))
    (or success
        (vlf-move-to-batch (- match-pos-start
                               (/ vlf-batch-size 2))))
    (let* ((match-end (- match-pos-end vlf-start-pos))
           (overlay (make-overlay (- match-pos-start vlf-start-pos)
                                  match-end)))
      (overlay-put overlay 'face 'region)
      (or success (goto-char match-end))
      (prog1 (cond (success t)
                   ((< to-find count)
                    (message "Moved to the %d match which is last"
                             (- count to-find))
                    t)
                   (t (message "Not found")
                      nil))
        (sit-for 0.1)
        (delete-overlay overlay)))))

(defun vlf-re-search-forward (regexp count)
  "Search forward for REGEXP prefix COUNT number of times.
Search is performed chunk by chunk in `vlf-batch-size' memory."
  (interactive (list (read-regexp "Search whole file"
                                  (if regexp-history
                                      (car regexp-history))
                                  'regexp-history)
                     (or current-prefix-arg 1)))
  (vlf-re-search regexp count nil))

(defun vlf-re-search-backward (regexp count)
  "Search backward for REGEXP prefix COUNT number of times.
Search is performed chunk by chunk in `vlf-batch-size' memory."
  (interactive (list (read-regexp "Search whole file backward"
                                  (if regexp-history
                                      (car regexp-history))
                                  'regexp-history)
                     (or current-prefix-arg 1)))
  (vlf-re-search regexp count t))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; editing

(defvar vlf-edit-mode-map
  (let ((map (make-sparse-keymap)))
    (set-keymap-parent map text-mode-map)
    (define-key map "\C-c\C-c" 'vlf-write)
    (define-key map "\C-c\C-q" 'vlf-discard-edit)
    map)
  "Keymap for command `vlf-edit-mode'.")

(define-derived-mode vlf-edit-mode vlf-mode "VLF[edit]"
  "Major mode for editing large file chunks."
  (setq buffer-read-only nil)
  (buffer-enable-undo)
  (message (substitute-command-keys
            "Editing: Type \\[vlf-write] to write chunk \
or \\[vlf-discard-edit] to discard changes.")))

(defun vlf-discard-edit ()
  "Discard edit and refresh chunk from file."
  (interactive)
  (vlf-move-to-chunk vlf-start-pos vlf-end-pos)
  (vlf-mode)
  (message "Switched to VLF mode."))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; saving

(defun vlf-write ()
  "Write current chunk to file.  Always return true to disable save.
If changing size of chunk shift remaining file content."
  (interactive)
  (when (and (derived-mode-p 'vlf-mode)
             (buffer-modified-p)
             (or (verify-visited-file-modtime)
                 (y-or-n-p "File has changed since visited or saved.  \
Save anyway? ")))
    (let ((size-change (- vlf-end-pos vlf-start-pos
                          (length (encode-coding-region
                                   (point-min) (point-max)
                                   buffer-file-coding-system t))))
          (pos (point)))
      (cond ((zerop size-change)
             (write-region nil nil buffer-file-name vlf-start-pos t))
            ((< 0 size-change)
             (vlf-file-shift-back size-change))
            (t (vlf-file-shift-forward (- size-change))))
      (goto-char pos))
    (vlf-move-to-chunk vlf-start-pos vlf-end-pos)
    (vlf-mode)
    t))

(defun vlf-file-shift-back (size-change)
  "Shift file contents SIZE-CHANGE bytes back."
  (let ((coding-system buffer-file-coding-system))
    (write-region nil nil buffer-file-name vlf-start-pos t)
    (setq buffer-file-coding-system nil)
    (let ((read-start-pos vlf-end-pos)
          (reporter (make-progress-reporter "Adjusting file content"
                                            vlf-end-pos
                                            vlf-file-size)))
      (while (vlf-shift-batch read-start-pos (- read-start-pos
                                                 size-change))
        (setq read-start-pos (+ read-start-pos vlf-batch-size))
        (progress-reporter-update reporter read-start-pos))
      ;; pad end with space
      (erase-buffer)
      (insert-char 32 size-change)
      (write-region nil nil buffer-file-name (- vlf-file-size
                                                size-change) t)
      (progress-reporter-done reporter))
    (setq buffer-file-coding-system coding-system)))

(defun vlf-shift-batch (read-pos write-pos)
  "Read `vlf-batch-size' bytes from READ-POS and write them \
back at WRITE-POS.  Return nil if EOF is reached, t otherwise."
  (erase-buffer)
  (setq vlf-file-size (nth 7 (file-attributes buffer-file-name)))
  (let ((read-end (+ read-pos vlf-batch-size)))
    (insert-file-contents-literally buffer-file-name nil
                                    read-pos
                                    (min vlf-file-size read-end))
    (write-region nil nil buffer-file-name write-pos t)
    (< read-end vlf-file-size)))

(defun vlf-file-shift-forward (size-change)
  "Shift file contents SIZE-CHANGE bytes forward.
Done by saving content up front and then writing previous batch."
  (let ((vlf-buffer (current-buffer))
        (temp-buffer (generate-new-buffer (concat " "
                                                  (buffer-name))))
        (coding-system buffer-file-coding-system))
    (let ((file buffer-file-name))
      (set-buffer temp-buffer)
      (setq buffer-file-name file))
    (set-buffer vlf-buffer)
    (let ((read-buffer temp-buffer)
          (write-buffer vlf-buffer)
          (size (+ vlf-batch-size size-change))
          (read-pos vlf-end-pos)
          (write-pos vlf-start-pos)
          swap-buffer
          (reporter (make-progress-reporter "Adjusting file content"
                                            vlf-start-pos
                                            vlf-file-size)))
      (while (vlf-shift-batches size read-buffer read-pos
                                 write-buffer write-pos)
        (setq swap-buffer read-buffer
              read-buffer write-buffer
              write-buffer swap-buffer
              write-pos (+ read-pos size-change)
              read-pos (+ read-pos size))
        (progress-reporter-update reporter write-pos))
      (progress-reporter-done reporter))
    (kill-buffer temp-buffer)
    (set-buffer vlf-buffer)
    (setq buffer-file-coding-system coding-system)))

(defun vlf-shift-batches (size read-buffer read-pos
                                write-buffer write-pos)
  "Read SIZE bytes in READ-BUFFER starting from READ-POS.
Then write contents of WRITE-BUFFER to buffer file at WRITE-POS.
Return nil if EOF is reached, t otherwise."
  (let* ((file-size (nth 7 (file-attributes buffer-file-name)))
         (read-more (< read-pos file-size)))
    (when read-more
      ;; read
      (set-buffer read-buffer)
      (erase-buffer)
      (setq buffer-file-coding-system nil)
      (insert-file-contents-literally buffer-file-name nil read-pos
                                      (min file-size (+ read-pos
                                                        size))))
    ;; write
    (set-buffer write-buffer)
    (write-region nil nil buffer-file-name write-pos t)
    read-more))

(provide 'vlf)

;;; vlf.el ends here
